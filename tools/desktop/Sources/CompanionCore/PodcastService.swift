import Foundation

public struct PodcastSearchResult: Identifiable, Hashable, Sendable {
    public let id: String
    public let title: String
    public let author: String
    public let feedURL: URL
    public let artworkURLString: String?
    public let genre: String?
    public let episodeCount: Int?
    public let description: String

    public init(
        id: String,
        title: String,
        author: String,
        feedURL: URL,
        artworkURLString: String?,
        genre: String?,
        episodeCount: Int?,
        description: String
    ) {
        self.id = id
        self.title = title
        self.author = author
        self.feedURL = feedURL
        self.artworkURLString = artworkURLString
        self.genre = genre
        self.episodeCount = episodeCount
        self.description = description
    }
}

public actor PodcastService {
    private let session: URLSession

    public init(session: URLSession = .shared) {
        self.session = session
    }

    public func searchDirectory(term: String) async throws -> [PodcastSearchResult] {
        let term = term.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !term.isEmpty else { return [] }
        var components = URLComponents(string: "https://itunes.apple.com/search")!
        components.queryItems = [
            URLQueryItem(name: "term", value: term),
            URLQueryItem(name: "media", value: "podcast"),
            URLQueryItem(name: "entity", value: "podcast"),
            URLQueryItem(name: "limit", value: "12")
        ]
        guard let url = components.url else { throw MetadataError.invalidQuery }
        var request = URLRequest(url: url, timeoutInterval: 20)
        request.setValue("HelikonCompanion/0.1.0 (https://github.com/deandromi/Helikon-OS)", forHTTPHeaderField: "User-Agent")
        let (data, response) = try await session.data(for: request)
        guard let http = response as? HTTPURLResponse, (200...299).contains(http.statusCode) else {
            throw MetadataError.invalidResponse
        }
        let decoded = try JSONDecoder().decode(PodcastITunesResponse.self, from: data)
        return (decoded.results ?? []).compactMap { item in
            guard let title = item.collectionName, let feed = item.feedUrl, let feedURL = URL(string: feed) else {
                return nil
            }
            let art = item.artworkUrl600 ?? item.artworkUrl100
            return PodcastSearchResult(
                id: item.collectionId.map(String.init) ?? feed,
                title: title,
                author: item.artistName ?? "",
                feedURL: feedURL,
                artworkURLString: art,
                genre: item.primaryGenreName,
                episodeCount: item.trackCount,
                description: item.description ?? ""
            )
        }
    }

    public func fetchFeed(url: URL, id: UUID = UUID()) async throws -> PodcastShow {
        var request = URLRequest(url: url, timeoutInterval: 30)
        request.setValue("HelikonCompanion/0.1.0 (https://github.com/deandromi/Helikon-OS)", forHTTPHeaderField: "User-Agent")
        let (data, response) = try await session.data(for: request)
        guard let http = response as? HTTPURLResponse, (200...299).contains(http.statusCode) else {
            throw MetadataError.invalidResponse
        }
        return try PodcastFeedParser.parse(data: data, feedURL: url, id: id)
    }

    public func refresh(_ show: PodcastShow) async throws -> PodcastShow {
        var refreshed = try await fetchFeed(url: show.feedURL, id: show.id)
        refreshed.subscribedAt = show.subscribedAt
        refreshed.lastRefreshed = Date()
        let existing = Dictionary(uniqueKeysWithValues: show.episodes.map { ($0.id, $0) })
        refreshed.episodes = refreshed.episodes.map { episode in
            guard let old = existing[episode.id] else { return episode }
            var merged = episode
            merged.localPath = old.localPath
            merged.downloadedAt = old.downloadedAt
            merged.downloadState = old.downloadState
            merged.error = old.error
            return merged
        }
        return refreshed
    }

    public func downloadEpisode(_ episode: PodcastEpisode, show: PodcastShow) async throws -> URL {
        guard let remote = episode.enclosureURL else {
            throw MetadataError.invalidResponse
        }
        let folder = AppPaths.downloadsDirectory
            .appendingPathComponent("Podcasts", isDirectory: true)
            .appendingPathComponent(FileNaming.sanitized(show.title), isDirectory: true)
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)

        var request = URLRequest(url: remote, timeoutInterval: 60)
        request.setValue("HelikonCompanion/0.1.0 (https://github.com/deandromi/Helikon-OS)", forHTTPHeaderField: "User-Agent")
        let (temporary, response) = try await session.download(for: request)
        if let http = response as? HTTPURLResponse, !(200...299).contains(http.statusCode) {
            throw MetadataError.http(http.statusCode)
        }
        let ext = remote.pathExtension.isEmpty ? "mp3" : remote.pathExtension
        let filename = "\(episode.publishedAt.map { DateFormatter.helikonDay.string(from: $0) + " " } ?? "")\(episode.title).\(ext)"
        let destination = FileNaming.uniqueDestination(
            for: URL(fileURLWithPath: FileNaming.sanitized(filename)),
            in: folder
        )
        if FileManager.default.fileExists(atPath: destination.path) {
            try FileManager.default.removeItem(at: destination)
        }
        try FileManager.default.moveItem(at: temporary, to: destination)
        return destination
    }
}

private struct PodcastITunesResponse: Decodable {
    let results: [PodcastITunesItem]?
}

private struct PodcastITunesItem: Decodable {
    let collectionId: Int?
    let collectionName: String?
    let artistName: String?
    let feedUrl: String?
    let artworkUrl600: String?
    let artworkUrl100: String?
    let primaryGenreName: String?
    let trackCount: Int?
    let description: String?
}

// MARK: - RSS / Atom parser

public final class PodcastFeedParser: NSObject, XMLParserDelegate {
    private struct DraftEpisode {
        var guid: String = ""
        var title: String = ""
        var summary: String = ""
        var publishedAt: Date?
        var duration: TimeInterval?
        var enclosureURL: URL?
        var link: String = ""
    }

    private var parser: XMLParser?
    private var inItem = false
    private var currentElement = ""
    private var buffer = ""
    private var channelTitle = ""
    private var channelAuthor = ""
    private var channelSummary = ""
    private var channelImage: String?
    private var episodes: [DraftEpisode] = []
    private var item = DraftEpisode()
    private var didParse = false
    private var parseError: Error?

    public static func parse(data: Data, feedURL: URL, id: UUID = UUID()) throws -> PodcastShow {
        let delegate = PodcastFeedParser()
        let parser = XMLParser(data: data)
        delegate.parser = parser
        parser.delegate = delegate
        parser.shouldProcessNamespaces = false
        parser.parse()
        if let error = delegate.parseError { throw error }
        guard delegate.didParse else { throw MetadataError.invalidResponse }

        let episodes = delegate.episodes.enumerated().map { index, draft in
            let identifier = draft.guid.isEmpty
                ? Hashing.stableID(feedURL.absoluteString, draft.title, String(index))
                : Hashing.stableID(draft.guid)
            return PodcastEpisode(
                id: identifier,
                title: draft.title.isEmpty ? "Episode \(index + 1)" : draft.title,
                summary: draft.summary,
                publishedAt: draft.publishedAt,
                duration: draft.duration,
                enclosureURL: draft.enclosureURL
            )
        }
        return PodcastShow(
            id: id,
            feedURL: feedURL,
            title: delegate.channelTitle.isEmpty ? feedURL.host ?? "Podcast" : delegate.channelTitle,
            author: delegate.channelAuthor,
            summary: delegate.channelSummary,
            artworkURLString: delegate.channelImage,
            subscribedAt: Date(),
            lastRefreshed: Date(),
            episodes: episodes
        )
    }

    public func parser(
        _ parser: XMLParser,
        didStartElement elementName: String,
        namespaceURI: String?,
        qualifiedName qName: String?,
        attributes attributeDict: [String: String] = [:]
    ) {
        let name = Self.localName(elementName)
        currentElement = name
        buffer = ""
        if name == "item" || name == "entry" {
            inItem = true
            item = DraftEpisode()
        }
        if name == "enclosure", let value = attributeDict["url"], let url = URL(string: value) {
            item.enclosureURL = url
        }
        if name == "image", let href = attributeDict["href"] {
            if inItem {
                // Episode artwork is not stored separately; it falls back to the show.
            } else {
                channelImage = href
            }
        }
        if name == "link", let href = attributeDict["href"], !inItem {
            _ = href
        }
        if name == "guid" {
            item.guid = ""
        }
    }

    public func parser(_ parser: XMLParser, foundCharacters string: String) {
        buffer += string
    }

    public func parser(_ parser: XMLParser, foundCDATA CDATABlock: Data) {
        if let value = String(data: CDATABlock, encoding: .utf8) {
            buffer += value
        }
    }

    public func parser(
        _ parser: XMLParser,
        didEndElement elementName: String,
        namespaceURI: String?,
        qualifiedName qName: String?
    ) {
        let name = Self.localName(elementName)
        let value = buffer.trimmingCharacters(in: .whitespacesAndNewlines)
        switch name {
        case "title":
            if inItem { item.title = value } else { channelTitle = value }
        case "description", "summary":
            if inItem { item.summary = Self.plainText(value) } else { channelSummary = Self.plainText(value) }
        case "author", "creator":
            if !inItem { channelAuthor = value }
        case "pubDate", "published", "updated":
            if inItem { item.publishedAt = Self.date(value) }
        case "duration":
            if inItem { item.duration = Self.duration(value) }
        case "guid", "id":
            if inItem { item.guid = value }
        case "link":
            if inItem { item.link = value }
        case "url":
            if !inItem, channelImage == nil, !value.isEmpty { channelImage = value }
        case "item", "entry":
            if inItem {
                if item.enclosureURL != nil {
                    episodes.append(item)
                }
                inItem = false
            }
            inItem = false
        case "channel", "feed":
            didParse = true
        default:
            break
        }
        buffer = ""
    }

    public func parser(_ parser: XMLParser, parseErrorOccurred parseError: Error) {
        self.parseError = parseError
    }

    private static func localName(_ value: String) -> String {
        value.split(separator: ":").last.map(String.init)?.lowercased() ?? value.lowercased()
    }

    private static func plainText(_ html: String) -> String {
        html
            .replacingOccurrences(of: "<[^>]+>", with: " ", options: .regularExpression)
            .replacingOccurrences(of: "\\s+", with: " ", options: .regularExpression)
            .trimmingCharacters(in: .whitespacesAndNewlines)
    }

    private static func date(_ value: String) -> Date? {
        let formats = [
            "EEE, dd MMM yyyy HH:mm:ss Z",
            "EEE, d MMM yyyy HH:mm:ss Z",
            "yyyy-MM-dd'T'HH:mm:ssZ",
            "yyyy-MM-dd'T'HH:mm:ss.SSSZ",
            "yyyy-MM-dd"
        ]
        for format in formats {
            let formatter = DateFormatter()
            formatter.locale = Locale(identifier: "en_US_POSIX")
            formatter.timeZone = TimeZone(secondsFromGMT: 0)
            formatter.dateFormat = format
            if let date = formatter.date(from: value) { return date }
        }
        return nil
    }

    private static func duration(_ value: String) -> TimeInterval? {
        if value.contains(":") {
            let parts = value.split(separator: ":").compactMap { Double($0) }
            guard !parts.isEmpty else { return nil }
            return parts.reduce(0) { $0 * 60 + $1 }
        }
        return Double(value)
    }
}

private extension DateFormatter {
    static let helikonDay: DateFormatter = {
        let formatter = DateFormatter()
        formatter.locale = Locale(identifier: "en_US_POSIX")
        formatter.dateFormat = "yyyy-MM-dd"
        return formatter
    }()
}
