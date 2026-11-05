import Foundation

public struct MetadataSearchResponse: Sendable {
    public var candidates: [MetadataCandidate]
    public var warnings: [String]

    public init(candidates: [MetadataCandidate] = [], warnings: [String] = []) {
        self.candidates = candidates
        self.warnings = warnings
    }
}

public enum MetadataError: LocalizedError {
    case invalidQuery
    case invalidResponse
    case http(Int)
    case decoding(String)
    case offline
    case artworkUnavailable

    public var errorDescription: String? {
        switch self {
        case .invalidQuery: return "The search needs a title or name."
        case .invalidResponse: return "The metadata service returned an unreadable response."
        case .http(let code): return "The metadata service returned HTTP \(code)."
        case .decoding(let detail): return "Could not read the metadata response. \(detail)"
        case .offline: return "The metadata service could not be reached."
        case .artworkUnavailable: return "Artwork is not available for this release."
        }
    }
}

public actor MetadataService {
    private let session: URLSession
    private let userAgent: String
    private var lastMusicBrainzRequest: Date?

    public init(session: URLSession = .shared) {
        self.session = session
        self.userAgent = "HelikonCompanion/0.1.0 (https://github.com/deandromi/Helikon-OS)"
    }

    public func searchTracks(
        title: String,
        artist: String,
        album: String = "",
        providers: Set<MetadataProvider>
    ) async -> MetadataSearchResponse {
        let title = title.trimmingCharacters(in: .whitespacesAndNewlines)
        let artist = artist.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !title.isEmpty || !artist.isEmpty else {
            return MetadataSearchResponse(warnings: [MetadataError.invalidQuery.localizedDescription])
        }

        var candidates: [MetadataCandidate] = []
        var warnings: [String] = []

        if providers.contains(.musicBrainz) {
            do {
                candidates += try await musicBrainzTracks(title: title, artist: artist, album: album)
            } catch {
                warnings.append("MusicBrainz: \(error.localizedDescription)")
            }
        }
        if providers.contains(.iTunes) {
            do {
                candidates += try await iTunesTracks(title: title, artist: artist)
            } catch {
                warnings.append("Apple: \(error.localizedDescription)")
            }
        }

        return MetadataSearchResponse(
            candidates: SearchRanking.rank(candidates, query: "\(title) \(artist)"),
            warnings: warnings
        )
    }

    public func searchAlbums(
        title: String,
        artist: String,
        providers: Set<MetadataProvider>
    ) async -> MetadataSearchResponse {
        let title = title.trimmingCharacters(in: .whitespacesAndNewlines)
        let artist = artist.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !title.isEmpty || !artist.isEmpty else {
            return MetadataSearchResponse(warnings: [MetadataError.invalidQuery.localizedDescription])
        }

        var candidates: [MetadataCandidate] = []
        var warnings: [String] = []

        if providers.contains(.musicBrainz) {
            do {
                candidates += try await musicBrainzAlbums(title: title, artist: artist)
            } catch {
                warnings.append("MusicBrainz: \(error.localizedDescription)")
            }
        }
        if providers.contains(.iTunes) {
            do {
                candidates += try await iTunesAlbums(title: title, artist: artist)
            } catch {
                warnings.append("Apple: \(error.localizedDescription)")
            }
        }

        return MetadataSearchResponse(
            candidates: SearchRanking.rank(candidates, query: "\(title) \(artist)"),
            warnings: warnings
        )
    }

    public func searchArtists(
        name: String,
        providers: Set<MetadataProvider>
    ) async -> MetadataSearchResponse {
        let name = name.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !name.isEmpty else {
            return MetadataSearchResponse(warnings: [MetadataError.invalidQuery.localizedDescription])
        }

        var candidates: [MetadataCandidate] = []
        var warnings: [String] = []

        if providers.contains(.musicBrainz) {
            do {
                candidates += try await musicBrainzArtists(name: name)
            } catch {
                warnings.append("MusicBrainz: \(error.localizedDescription)")
            }
        }
        if providers.contains(.iTunes) {
            do {
                candidates += try await iTunesArtists(name: name)
            } catch {
                warnings.append("Apple: \(error.localizedDescription)")
            }
        }

        return MetadataSearchResponse(
            candidates: SearchRanking.rank(candidates, query: name),
            warnings: warnings
        )
    }

    // MARK: - MusicBrainz

    private func musicBrainzTracks(title: String, artist: String, album: String) async throws -> [MetadataCandidate] {
        var clauses: [String] = []
        if !title.isEmpty { clauses.append("recording:\"\(escaped(title))\"") }
        if !artist.isEmpty { clauses.append("artist:\"\(escaped(artist))\"") }
        if !album.isEmpty { clauses.append("release:\"\(escaped(album))\"") }
        let query = clauses.joined(separator: " AND ")
        let data = try await musicBrainzRequest(path: "recording", query: query, limit: 8)
        let response = try decode(MBRecordingResponse.self, from: data)

        return (response.recordings ?? []).map { recording in
            let credit = recording.artistCredit?.compactMap(\.name).joined(separator: ", ") ?? ""
            let release = recording.releases?.first
            let releaseID = release?.id
            var patch: [MetadataField: String] = [:]
            patch[.title] = recording.title
            if !credit.isEmpty { patch[.artist] = credit; patch[.albumArtist] = credit }
            if let releaseTitle = release?.title, !releaseTitle.isEmpty { patch[.album] = releaseTitle }
            if let year = release?.date?.prefix(4), !year.isEmpty { patch[.year] = String(year) }
            if let releaseID, !releaseID.isEmpty {
                patch[.artwork] = "https://coverartarchive.org/release/\(releaseID)/front-250"
            }
            return MetadataCandidate(
                id: "mb-recording-\(recording.id)",
                source: .musicBrainz,
                entity: .track,
                title: recording.title,
                subtitle: credit.isEmpty ? "MusicBrainz recording" : credit,
                detail: release?.title,
                year: release?.date,
                genre: nil,
                artworkURLString: releaseID.map { "https://coverartarchive.org/release/\($0)/front-250" },
                externalID: recording.id,
                score: Double(recording.score ?? 0) / 100,
                patch: patch,
                trackCount: nil,
                duration: recording.length.map { Double($0) / 1000 }
            )
        }
    }

    private func musicBrainzAlbums(title: String, artist: String) async throws -> [MetadataCandidate] {
        var clauses: [String] = []
        if !title.isEmpty { clauses.append("release:\"\(escaped(title))\"") }
        if !artist.isEmpty { clauses.append("artist:\"\(escaped(artist))\"") }
        let data = try await musicBrainzRequest(
            path: "release",
            query: clauses.joined(separator: " AND "),
            limit: 8
        )
        let response = try decode(MBReleaseResponse.self, from: data)
        return (response.releases ?? []).map { release in
            let credit = release.artistCredit?.compactMap(\.name).joined(separator: ", ") ?? ""
            let year = release.date?.prefix(4).description
            var patch: [MetadataField: String] = [:]
            patch[.album] = release.title
            if !credit.isEmpty { patch[.albumArtist] = credit }
            if let year, !year.isEmpty { patch[.year] = year }
            if let genre = release.releaseGroup?.primaryType, !genre.isEmpty { patch[.genre] = genre.capitalized }
            if let id = release.id {
                patch[.artwork] = "https://coverartarchive.org/release/\(id)/front-500"
            }
            return MetadataCandidate(
                id: "mb-release-\(release.id ?? "unknown")",
                source: .musicBrainz,
                entity: .album,
                title: release.title,
                subtitle: credit.isEmpty ? "MusicBrainz release" : credit,
                detail: release.releaseGroup?.primaryType,
                year: year,
                genre: release.releaseGroup?.primaryType?.capitalized,
                artworkURLString: release.id.map { "https://coverartarchive.org/release/\($0)/front-500" },
                externalID: release.id,
                score: Double(release.score ?? 0) / 100,
                patch: patch,
                trackCount: release.trackCount
            )
        }
    }

    private func musicBrainzArtists(name: String) async throws -> [MetadataCandidate] {
        let data = try await musicBrainzRequest(
            path: "artist",
            query: "artist:\"\(escaped(name))\"",
            limit: 8
        )
        let response = try decode(MBArtistResponse.self, from: data)
        return (response.artists ?? []).map { artist in
            let detailParts = [
                artist.type?.capitalized,
                artist.country,
                artist.disambiguation
            ].compactMap { $0 }.filter { !$0.isEmpty }
            var patch: [MetadataField: String] = [:]
            patch[.artist] = artist.name
            if let type = artist.type, !type.isEmpty {
                patch[.genre] = type.capitalized
            }
            let lifespan: String? = {
                guard let begin = artist.lifeSpan?.begin, !begin.isEmpty else { return nil }
                if let end = artist.lifeSpan?.end, !end.isEmpty { return "\(begin)–\(end)" }
                return begin
            }()
            return MetadataCandidate(
                id: "mb-artist-\(artist.id)",
                source: .musicBrainz,
                entity: .artist,
                title: artist.name,
                subtitle: detailParts.isEmpty ? "MusicBrainz artist" : detailParts.joined(separator: " · "),
                detail: lifespan,
                year: nil,
                genre: artist.type?.capitalized,
                artworkURLString: nil,
                externalID: artist.id,
                score: Double(artist.score ?? 0) / 100,
                patch: patch
            )
        }
    }

    private func musicBrainzRequest(path: String, query: String, limit: Int) async throws -> Data {
        if let last = lastMusicBrainzRequest {
            let elapsed = Date().timeIntervalSince(last)
            if elapsed < 1.05 {
                let nanoseconds = UInt64((1.05 - elapsed) * 1_000_000_000)
                try? await Task.sleep(nanoseconds: nanoseconds)
            }
        }
        lastMusicBrainzRequest = Date()

        var components = URLComponents(string: "https://musicbrainz.org/ws/2/\(path)/")!
        components.queryItems = [
            URLQueryItem(name: "query", value: query),
            URLQueryItem(name: "fmt", value: "json"),
            URLQueryItem(name: "limit", value: String(limit))
        ]
        guard let url = components.url else { throw MetadataError.invalidQuery }
        var request = URLRequest(url: url, timeoutInterval: 20)
        request.setValue(userAgent, forHTTPHeaderField: "User-Agent")
        request.setValue("application/json", forHTTPHeaderField: "Accept")
        let (data, response) = try await session.data(for: request)
        try validate(response)
        return data
    }

    // MARK: - Apple search API

    private func iTunesTracks(title: String, artist: String) async throws -> [MetadataCandidate] {
        let term = [artist, title].filter { !$0.isEmpty }.joined(separator: " ")
        let items = try await iTunesSearch(term: term, media: "music", entity: "song", limit: 8)
        return items.compactMap { item in
            guard let trackName = item.trackName else { return nil }
            let artistName = item.artistName ?? ""
            let collection = item.collectionName ?? ""
            var patch: [MetadataField: String] = [
                .title: trackName,
                .artist: artistName
            ]
            if !collection.isEmpty { patch[.album] = collection }
            if !artistName.isEmpty { patch[.albumArtist] = artistName }
            if let date = item.releaseDate?.prefix(4), !date.isEmpty { patch[.year] = String(date) }
            if let genre = item.primaryGenreName, !genre.isEmpty { patch[.genre] = genre }
            if let art = item.artworkURL {
                patch[.artwork] = art
            }
            return MetadataCandidate(
                id: "itunes-track-\(item.trackId.map(String.init) ?? trackName)",
                source: .iTunes,
                entity: .track,
                title: trackName,
                subtitle: artistName.isEmpty ? "Apple Music" : artistName,
                detail: collection.isEmpty ? nil : collection,
                year: item.releaseDate?.prefix(4).description,
                genre: item.primaryGenreName,
                artworkURLString: item.artworkURL,
                externalID: item.trackId.map(String.init),
                score: 0.78,
                patch: patch,
                duration: item.trackTimeMillis.map { Double($0) / 1000 }
            )
        }
    }

    private func iTunesAlbums(title: String, artist: String) async throws -> [MetadataCandidate] {
        let term = [artist, title].filter { !$0.isEmpty }.joined(separator: " ")
        let items = try await iTunesSearch(term: term, media: "music", entity: "album", limit: 8)
        return items.compactMap { item in
            guard let collection = item.collectionName else { return nil }
            let artistName = item.artistName ?? ""
            var patch: [MetadataField: String] = [.album: collection]
            if !artistName.isEmpty { patch[.albumArtist] = artistName }
            if let year = item.releaseDate?.prefix(4), !year.isEmpty { patch[.year] = String(year) }
            if let genre = item.primaryGenreName, !genre.isEmpty { patch[.genre] = genre }
            if let art = item.artworkURL { patch[.artwork] = art }
            return MetadataCandidate(
                id: "itunes-album-\(item.collectionId.map(String.init) ?? collection)",
                source: .iTunes,
                entity: .album,
                title: collection,
                subtitle: artistName.isEmpty ? "Apple Music" : artistName,
                detail: item.primaryGenreName,
                year: item.releaseDate?.prefix(4).description,
                genre: item.primaryGenreName,
                artworkURLString: item.artworkURL,
                externalID: item.collectionId.map(String.init),
                score: 0.8,
                patch: patch,
                trackCount: item.trackCount
            )
        }
    }

    private func iTunesArtists(name: String) async throws -> [MetadataCandidate] {
        let items = try await iTunesSearch(term: name, media: "music", entity: "musicArtist", limit: 8)
        return items.compactMap { item in
            guard let artistName = item.artistName else { return nil }
            var patch: [MetadataField: String] = [.artist: artistName]
            if let genre = item.primaryGenreName, !genre.isEmpty {
                patch[.genre] = genre
            }
            return MetadataCandidate(
                id: "itunes-artist-\(item.artistId.map(String.init) ?? artistName)",
                source: .iTunes,
                entity: .artist,
                title: artistName,
                subtitle: item.primaryGenreName ?? "Apple Music artist",
                detail: nil,
                year: nil,
                genre: item.primaryGenreName,
                artworkURLString: nil,
                externalID: item.artistId.map(String.init),
                score: 0.8,
                patch: patch
            )
        }
    }

    private func iTunesSearch(term: String, media: String, entity: String, limit: Int) async throws -> [ITunesItem] {
        guard !term.isEmpty else { return [] }
        var components = URLComponents(string: "https://itunes.apple.com/search")!
        components.queryItems = [
            URLQueryItem(name: "term", value: term),
            URLQueryItem(name: "media", value: media),
            URLQueryItem(name: "entity", value: entity),
            URLQueryItem(name: "limit", value: String(limit))
        ]
        guard let url = components.url else { throw MetadataError.invalidQuery }
        var request = URLRequest(url: url, timeoutInterval: 20)
        request.setValue(userAgent, forHTTPHeaderField: "User-Agent")
        let (data, response) = try await session.data(for: request)
        try validate(response)
        return (try decode(ITunesSearchResponse.self, from: data)).results ?? []
    }

    // MARK: - Network

    private func validate(_ response: URLResponse) throws {
        guard let http = response as? HTTPURLResponse else { throw MetadataError.invalidResponse }
        guard (200...299).contains(http.statusCode) else { throw MetadataError.http(http.statusCode) }
    }

    private func decode<T: Decodable>(_ type: T.Type, from data: Data) throws -> T {
        let decoder = JSONDecoder()
        decoder.keyDecodingStrategy = .convertFromSnakeCase
        do {
            return try decoder.decode(type, from: data)
        } catch {
            throw MetadataError.decoding(error.localizedDescription)
        }
    }

    private func escaped(_ value: String) -> String {
        value.replacingOccurrences(of: "\"", with: "\\\"")
    }
}

// MARK: - Apple response

private struct ITunesSearchResponse: Decodable {
    let results: [ITunesItem]?
}

private struct ITunesItem: Decodable {
    let wrapperType: String?
    let kind: String?
    let trackId: Int?
    let collectionId: Int?
    let artistId: Int?
    let trackName: String?
    let collectionName: String?
    let artistName: String?
    let releaseDate: String?
    let primaryGenreName: String?
    let artworkUrl100: String?
    let trackTimeMillis: Int?
    let trackCount: Int?
    let feedUrl: String?
    let collectionViewUrl: String?
    let trackViewUrl: String?

    var artworkURL: String? {
        guard var value = artworkUrl100 else { return nil }
        value = value.replacingOccurrences(of: "100x100bb", with: "600x600bb")
        value = value.replacingOccurrences(of: "100x100-75", with: "600x600-75")
        return value.hasPrefix("http") ? value : nil
    }
}

// MARK: - MusicBrainz response

private struct MBRecordingResponse: Decodable { let recordings: [MBRecording]? }
private struct MBReleaseResponse: Decodable { let releases: [MBRelease]? }
private struct MBArtistResponse: Decodable { let artists: [MBArtist]? }

private struct MBRecording: Decodable {
    let id: String
    let title: String
    let length: Int?
    let score: Int?
    let artistCredit: [MBArtistCredit]?
    let releases: [MBRelease]?

    enum CodingKeys: String, CodingKey {
        case id, title, length, score, releases
        case artistCredit = "artist-credit"
    }
}

private struct MBRelease: Decodable {
    let id: String?
    let title: String
    let date: String?
    let score: Int?
    let artistCredit: [MBArtistCredit]?
    let releaseGroup: MBReleaseGroup?
    let trackCount: Int?

    enum CodingKeys: String, CodingKey {
        case id, title, date, score
        case artistCredit = "artist-credit"
        case releaseGroup = "release-group"
        case trackCount = "track-count"
    }
}

private struct MBReleaseGroup: Decodable {
    let primaryType: String?
    let secondaryTypes: [String]?

    enum CodingKeys: String, CodingKey {
        case primaryType = "primary-type"
        case secondaryTypes = "secondary-types"
    }
}

private struct MBArtistCredit: Decodable {
    let name: String?
    let artist: MBArtist?
}

private struct MBArtist: Decodable {
    let id: String
    let name: String
    let score: Int?
    let type: String?
    let country: String?
    let disambiguation: String?
    let lifeSpan: MBLifeSpan?

    enum CodingKeys: String, CodingKey {
        case id, name, score, type, country, disambiguation
        case lifeSpan = "life-span"
    }
}

private struct MBLifeSpan: Decodable {
    let begin: String?
    let end: String?
}

// MARK: - Ranking

public enum SearchRanking {
    public static func rank(_ candidates: [MetadataCandidate], query: String) -> [MetadataCandidate] {
        let queryTokens = tokens(query)
        return candidates
            .map { candidate -> MetadataCandidate in
                var copy = candidate
                let candidateTokens = tokens("\(candidate.title) \(candidate.subtitle)")
                let overlap = queryTokens.isEmpty ? 0 : Double(queryTokens.intersection(candidateTokens).count) / Double(queryTokens.count)
                copy.score = max(0, min(1, candidate.score * 0.55 + overlap * 0.45))
                return copy
            }
            .sorted { lhs, rhs in
                if lhs.score != rhs.score { return lhs.score > rhs.score }
                return lhs.title.localizedCaseInsensitiveCompare(rhs.title) == .orderedAscending
            }
    }

    private static func tokens(_ value: String) -> Set<String> {
        let folded = value.folding(options: [.caseInsensitive, .diacriticInsensitive], locale: .current)
        let tokens = folded.components(separatedBy: CharacterSet.alphanumerics.inverted)
            .filter { !$0.isEmpty }
        return Set(tokens)
    }
}

// MARK: - Artwork cache

public enum ArtworkStore {
    public static func cache(
        remoteURLString: String,
        session: URLSession = .shared
    ) async throws -> String {
        guard let url = URL(string: remoteURLString), url.scheme?.hasPrefix("http") == true else {
            throw MetadataError.artworkUnavailable
        }
        let key = Hashing.sha256(of: Data(remoteURLString.utf8)).prefix(32)
        try FileManager.default.createDirectory(at: AppPaths.artworkDirectory, withIntermediateDirectories: true)

        if let existing = try? FileManager.default.contentsOfDirectory(at: AppPaths.artworkDirectory, includingPropertiesForKeys: nil)
            .first(where: { $0.deletingPathExtension().lastPathComponent == String(key) }) {
            return existing.path
        }

        var request = URLRequest(url: url, timeoutInterval: 30)
        request.setValue("HelikonCompanion/0.1.0", forHTTPHeaderField: "User-Agent")
        let (data, response) = try await session.data(for: request)
        if let http = response as? HTTPURLResponse, !(200...299).contains(http.statusCode) {
            throw MetadataError.http(http.statusCode)
        }
        guard !data.isEmpty, data.count < 12 * 1024 * 1024 else {
            throw MetadataError.artworkUnavailable
        }
        let ext = data.starts(with: [0x89, 0x50, 0x4E, 0x47]) ? "png" : "jpg"
        let local = AppPaths.artworkDirectory.appendingPathComponent("\(key).\(ext)")
        try data.write(to: local, options: [.atomic])
        return local.path
    }
}
