import Foundation

public actor AudiobookService {
    private let session: URLSession

    public init(session: URLSession = .shared) {
        self.session = session
    }

    public func searchOpenLibrary(term: String) async throws -> [Audiobook] {
        let term = term.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !term.isEmpty else { return [] }
        var components = URLComponents(string: "https://openlibrary.org/search.json")!
        components.queryItems = [
            URLQueryItem(name: "q", value: term),
            URLQueryItem(name: "limit", value: "12"),
            URLQueryItem(name: "fields", value: "key,title,author_name,first_publish_year,cover_i,subject,language,edition_count")
        ]
        guard let url = components.url else { throw MetadataError.invalidQuery }
        var request = URLRequest(url: url, timeoutInterval: 25)
        request.setValue("HelikonCompanion/0.1.0 (https://github.com/deandromi/Helikon-OS)", forHTTPHeaderField: "User-Agent")
        let (data, response) = try await session.data(for: request)
        guard let http = response as? HTTPURLResponse, (200...299).contains(http.statusCode) else {
            throw MetadataError.invalidResponse
        }
        let decoded = try JSONDecoder().decode(OpenLibraryResponse.self, from: data)
        return (decoded.docs ?? []).compactMap { doc in
            guard let title = doc.title else { return nil }
            let authors = (doc.authorName ?? []).filter { !$0.isEmpty }
            let year = doc.firstPublishYear.map(String.init)
            let cover = doc.coverI.map { "https://covers.openlibrary.org/b/id/\($0)-L.jpg" }
            let summary = (doc.subject ?? []).prefix(6).joined(separator: " · ")
            return Audiobook(
                id: "ol-\(doc.key ?? title)",
                title: title,
                author: authors.isEmpty ? "Unknown author" : authors.joined(separator: ", "),
                year: year,
                summary: summary,
                source: .openLibrary,
                sourceURLString: doc.key.map { "https://openlibrary.org\($0)" },
                coverURLString: cover,
                isPublicDomain: false
            )
        }
    }

    public func searchLibriVox(term: String) async throws -> [Audiobook] {
        let term = term.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !term.isEmpty else { return [] }
        var components = URLComponents(string: "https://librivox.org/api/feed/audiobooks/")!
        components.queryItems = [
            URLQueryItem(name: "title", value: term),
            URLQueryItem(name: "format", value: "json"),
            URLQueryItem(name: "extended", value: "1"),
            URLQueryItem(name: "limit", value: "12")
        ]
        guard let url = components.url else { throw MetadataError.invalidQuery }
        var request = URLRequest(url: url, timeoutInterval: 25)
        request.setValue("HelikonCompanion/0.1.0 (https://github.com/deandromi/Helikon-OS)", forHTTPHeaderField: "User-Agent")
        let (data, response) = try await session.data(for: request)
        guard let http = response as? HTTPURLResponse, (200...299).contains(http.statusCode) else {
            throw MetadataError.invalidResponse
        }
        let decoded = try JSONDecoder().decode(LibriVoxResponse.self, from: data)
        return (decoded.books ?? []).compactMap { book in
            guard let title = book.title else { return nil }
            let authors = (book.authors ?? []).map { author in
                [author.firstName, author.lastName].compactMap { $0 }.joined(separator: " ")
            }.filter { !$0.isEmpty }
            let sections = (book.sections ?? []).sorted {
                (Int($0.sectionNumber ?? "0") ?? 0) < (Int($1.sectionNumber ?? "0") ?? 0)
            }
            let urls = sections.compactMap(\.listenUrl).filter { !$0.isEmpty }
            let duration = sections.compactMap { section -> TimeInterval? in
                guard let playtime = section.playtime else { return nil }
                return Self.parsePlaytime(playtime)
            }.reduce(0, +)
            let summary = Self.plainText(book.description ?? "")
            return Audiobook(
                id: "lv-\(book.id ?? title)",
                title: title,
                author: authors.isEmpty ? "Unknown author" : authors.joined(separator: ", "),
                year: book.copyrightYear,
                summary: summary,
                source: .librivox,
                sourceURLString: book.urlLibrivox,
                remoteFileURLs: urls,
                duration: duration > 0 ? duration : nil,
                isPublicDomain: true
            )
        }
    }

    public func download(_ audiobook: Audiobook, progress: (@Sendable (Int, Int) -> Void)? = nil) async throws -> Audiobook {
        guard !audiobook.remoteFileURLs.isEmpty else { return audiobook }
        let folder = AppPaths.downloadsDirectory
            .appendingPathComponent("Audiobooks", isDirectory: true)
            .appendingPathComponent(FileNaming.sanitized(audiobook.title), isDirectory: true)
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)

        var downloaded: [String] = []
        for (index, value) in audiobook.remoteFileURLs.enumerated() {
            guard let remote = URL(string: value) else { continue }
            var request = URLRequest(url: remote, timeoutInterval: 90)
            request.setValue("HelikonCompanion/0.1.0 (https://github.com/deandromi/Helikon-OS)", forHTTPHeaderField: "User-Agent")
            let (temporary, response) = try await session.download(for: request)
            if let http = response as? HTTPURLResponse, !(200...299).contains(http.statusCode) {
                throw MetadataError.http(http.statusCode)
            }
            let ext = remote.pathExtension.isEmpty ? "mp3" : remote.pathExtension
            let name = String(format: "%03d - %@.%@", index + 1, FileNaming.sanitized(audiobook.title), ext)
            let destination = folder.appendingPathComponent(name)
            if FileManager.default.fileExists(atPath: destination.path) {
                try FileManager.default.removeItem(at: destination)
            }
            try FileManager.default.moveItem(at: temporary, to: destination)
            downloaded.append(destination.path)
            progress?(index + 1, audiobook.remoteFileURLs.count)
        }

        var updated = audiobook
        updated.localPaths = downloaded
        updated.coverPath = try? await cacheCover(for: updated)
        return updated
    }

    public func cacheCover(for audiobook: Audiobook) async throws -> String? {
        guard let cover = audiobook.coverURLString else { return audiobook.coverPath }
        return try await ArtworkStore.cache(remoteURLString: cover, session: session)
    }

    private static func parsePlaytime(_ value: String) -> TimeInterval? {
        if value.contains(":") {
            let parts = value.split(separator: ":").compactMap { Double($0) }
            guard !parts.isEmpty else { return nil }
            return parts.reduce(0) { $0 * 60 + $1 }
        }
        return Double(value)
    }

    private static func plainText(_ value: String) -> String {
        value
            .replacingOccurrences(of: "<[^>]+>", with: " ", options: .regularExpression)
            .replacingOccurrences(of: "\\s+", with: " ", options: .regularExpression)
            .trimmingCharacters(in: .whitespacesAndNewlines)
    }
}

private struct OpenLibraryResponse: Decodable {
    let docs: [OpenLibraryDoc]?
}

private struct OpenLibraryDoc: Decodable {
    let key: String?
    let title: String?
    let authorName: [String]?
    let firstPublishYear: Int?
    let coverI: Int?
    let subject: [String]?
    let language: [String]?
    let editionCount: Int?

    enum CodingKeys: String, CodingKey {
        case key, title, subject, language
        case authorName = "author_name"
        case firstPublishYear = "first_publish_year"
        case coverI = "cover_i"
        case editionCount = "edition_count"
    }
}

private struct LibriVoxResponse: Decodable {
    let books: [LibriVoxBook]?
}

private struct LibriVoxBook: Decodable {
    let id: String?
    let title: String?
    let description: String?
    let copyrightYear: String?
    let urlLibrivox: String?
    let authors: [LibriVoxAuthor]?
    let sections: [LibriVoxSection]?

    enum CodingKeys: String, CodingKey {
        case id, title, description, authors, sections
        case copyrightYear = "copyright_year"
        case urlLibrivox = "url_librivox"
    }
}

private struct LibriVoxAuthor: Decodable {
    let firstName: String?
    let lastName: String?

    enum CodingKeys: String, CodingKey {
        case firstName = "first_name"
        case lastName = "last_name"
    }
}

private struct LibriVoxSection: Decodable {
    let sectionNumber: String?
    let title: String?
    let listenUrl: String?
    let playtime: String?

    enum CodingKeys: String, CodingKey {
        case title, playtime
        case sectionNumber = "section_number"
        case listenUrl = "listen_url"
    }
}
