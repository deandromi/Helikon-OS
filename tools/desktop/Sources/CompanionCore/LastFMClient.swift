import Foundation
import Security

public struct LastFMSession: Codable, Hashable, Sendable {
    public let name: String
    public let key: String
    public let subscriber: Int?

    public init(name: String, key: String, subscriber: Int? = nil) {
        self.name = name
        self.key = key
        self.subscriber = subscriber
    }
}

public enum LastFMError: LocalizedError {
    case missingCredentials
    case service(String)
    case invalidResponse
    case http(Int)

    public var errorDescription: String? {
        switch self {
        case .missingCredentials: return "Add a Last.fm API key and secret in Settings first."
        case .service(let message): return "Last.fm: \(message)"
        case .invalidResponse: return "Last.fm returned an unreadable response."
        case .http(let code): return "Last.fm returned HTTP \(code)."
        }
    }
}

public actor LastFMClient {
    private let session: URLSession
    private let endpoint = URL(string: "https://ws.audioscrobbler.com/2.0/")!

    public init(session: URLSession = .shared) {
        self.session = session
    }

    public func authenticationToken(apiKey: String, secret: String) async throws -> String {
        guard !apiKey.isEmpty, !secret.isEmpty else { throw LastFMError.missingCredentials }
        let params = ["method": "auth.getToken", "api_key": apiKey]
        let payload = try await post(params: params, secret: secret)
        guard let token = payload["token"] as? String, !token.isEmpty else {
            throw LastFMError.service(payload["message"] as? String ?? "No token returned.")
        }
        return token
    }

    public func authenticationURL(apiKey: String, token: String) -> URL? {
        var components = URLComponents(string: "https://www.last.fm/api/auth/")
        components?.queryItems = [
            URLQueryItem(name: "api_key", value: apiKey),
            URLQueryItem(name: "token", value: token)
        ]
        return components?.url
    }

    public func session(apiKey: String, secret: String, token: String) async throws -> LastFMSession {
        let params = ["method": "auth.getSession", "api_key": apiKey, "token": token]
        let payload = try await post(params: params, secret: secret)
        guard let session = payload["session"] as? [String: Any],
              let name = session["name"] as? String,
              let key = session["key"] as? String else {
            throw LastFMError.service(payload["message"] as? String ?? "No session returned.")
        }
        let subscriber = (session["subscriber"] as? NSNumber)?.intValue
        return LastFMSession(name: name, key: key, subscriber: subscriber)
    }

    public func scrobble(
        _ entries: [ScrobbleEntry],
        apiKey: String,
        secret: String,
        sessionKey: String
    ) async throws -> Int {
        guard !apiKey.isEmpty, !secret.isEmpty, !sessionKey.isEmpty else {
            throw LastFMError.missingCredentials
        }
        var accepted = 0
        for batch in entries.chunked(into: 50) {
            var params: [String: String] = [
                "method": "track.scrobble",
                "api_key": apiKey,
                "sk": sessionKey
            ]
            for (index, entry) in batch.enumerated() {
                params["artist[\(index)]"] = entry.artist
                params["track[\(index)]"] = entry.title
                params["timestamp[\(index)]"] = String(Int(entry.timestamp.timeIntervalSince1970))
                if !entry.album.isEmpty { params["album[\(index)]"] = entry.album }
                if let duration = entry.duration, duration > 0 {
                    params["duration[\(index)]"] = String(Int(duration))
                }
            }
            let payload = try await post(params: params, secret: secret)
            if let error = payload["error"] as? NSNumber {
                let message = payload["message"] as? String ?? "Scrobble rejected."
                throw LastFMError.service("\(error.intValue): \(message)")
            }
            if let scrobbles = payload["scrobbles"] as? [String: Any],
               let attributes = scrobbles["@attr"] as? [String: Any],
               let value = attributes["accepted"] as? NSNumber {
                accepted += value.intValue
            } else {
                accepted += batch.count
            }
        }
        return accepted
    }

    public func updateNowPlaying(
        artist: String,
        title: String,
        album: String,
        apiKey: String,
        secret: String,
        sessionKey: String
    ) async throws {
        guard !apiKey.isEmpty, !secret.isEmpty, !sessionKey.isEmpty else {
            throw LastFMError.missingCredentials
        }
        var params = [
            "method": "track.updateNowPlaying",
            "api_key": apiKey,
            "sk": sessionKey,
            "artist": artist,
            "track": title
        ]
        if !album.isEmpty { params["album"] = album }
        let payload = try await post(params: params, secret: secret)
        if payload["error"] != nil {
            throw LastFMError.service(payload["message"] as? String ?? "Now-playing update rejected.")
        }
    }

    public func signature(params: [String: String], secret: String) -> String {
        let filtered = params.filter { key, _ in
            key != "format" && key != "callback" && key != "api_sig"
        }
        let canonical = filtered.keys.sorted().map { key in
            "\(key)\(filtered[key] ?? "")"
        }.joined()
        return Hashing.md5(canonical + secret)
    }

    private func post(params: [String: String], secret: String) async throws -> [String: Any] {
        var allParams = params
        allParams["format"] = "json"
        allParams["api_sig"] = signature(params: params, secret: secret)
        var request = URLRequest(url: endpoint, timeoutInterval: 30)
        request.httpMethod = "POST"
        request.setValue("application/x-www-form-urlencoded; charset=utf-8", forHTTPHeaderField: "Content-Type")
        request.setValue("HelikonCompanion/0.1.0 (https://github.com/deandromi/Helikon-OS)", forHTTPHeaderField: "User-Agent")
        request.httpBody = allParams
            .map { key, value in "\(Self.escape(key))=\(Self.escape(value))" }
            .joined(separator: "&")
            .data(using: .utf8)
        let (data, response) = try await session.data(for: request)
        guard let http = response as? HTTPURLResponse else { throw LastFMError.invalidResponse }
        let object = (try? JSONSerialization.jsonObject(with: data)) as? [String: Any]
        if !(200...299).contains(http.statusCode) {
            if let message = object?["message"] as? String, !message.isEmpty {
                throw LastFMError.service(message)
            }
            throw LastFMError.http(http.statusCode)
        }
        guard let object else { throw LastFMError.invalidResponse }
        return object
    }

    private static func escape(_ value: String) -> String {
        var allowed = CharacterSet.alphanumerics
        allowed.insert(charactersIn: "-._~")
        return value.addingPercentEncoding(withAllowedCharacters: allowed) ?? value
    }
}

public enum ScrobbleImporter {
    public static func parseRockboxLog(_ data: Data) -> [ScrobbleEntry] {
        guard let text = String(data: data, encoding: .utf8) else { return [] }
        var entries: [ScrobbleEntry] = []
        for line in text.components(separatedBy: .newlines) {
            let trimmed = line.trimmingCharacters(in: .whitespacesAndNewlines)
            if trimmed.isEmpty || trimmed.hasPrefix("#") { continue }
            let fields = trimmed.components(separatedBy: "\t")
            guard fields.count >= 5 else { continue }

            let artist = fields[0].trimmingCharacters(in: .whitespaces)
            let album = fields[1].trimmingCharacters(in: .whitespaces)
            let title = fields[2].trimmingCharacters(in: .whitespaces)
            let length = Double(fields[3].trimmingCharacters(in: .whitespaces))
            let timestampValue = fields.count >= 6 ? fields[5] : fields[4]
            guard !artist.isEmpty, !title.isEmpty, let timestamp = parseTimestamp(timestampValue) else { continue }

            entries.append(ScrobbleEntry(
                artist: artist,
                title: title,
                album: album,
                timestamp: timestamp,
                duration: length,
                source: .importedLog,
                status: .queued
            ))
        }
        return entries
    }

    public static func parseJSON(_ data: Data) -> [ScrobbleEntry] {
        struct Imported: Decodable {
            let artist: String
            let title: String
            let album: String?
            let timestamp: Double?
            let playedAt: String?
            let duration: Double?
        }
        guard let rows = try? JSONDecoder().decode([Imported].self, from: data) else { return [] }
        var entries: [ScrobbleEntry] = []
        for row in rows {
            let timestamp: Date?
            if let value = row.timestamp {
                timestamp = Date(timeIntervalSince1970: value > 10_000_000_000 ? value / 1000 : value)
            } else if let value = row.playedAt {
                timestamp = ISO8601DateFormatter().date(from: value)
            } else {
                timestamp = nil
            }
            guard let timestamp else { continue }
            entries.append(ScrobbleEntry(
                artist: row.artist,
                title: row.title,
                album: row.album ?? "",
                timestamp: timestamp,
                duration: row.duration,
                source: .playHistory,
                status: .queued
            ))
        }
        return entries
    }

    private static func parseTimestamp(_ value: String) -> Date? {
        let trimmed = value.trimmingCharacters(in: .whitespaces)
        if let seconds = Double(trimmed) {
            return Date(timeIntervalSince1970: seconds > 10_000_000_000 ? seconds / 1000 : seconds)
        }
        if let date = ISO8601DateFormatter().date(from: trimmed) { return date }
        let formats = ["yyyy-MM-dd HH:mm:ss", "yyyy-MM-dd'T'HH:mm:ss", "yyyy-MM-dd"]
        for format in formats {
            let formatter = DateFormatter()
            formatter.locale = Locale(identifier: "en_US_POSIX")
            formatter.dateFormat = format
            if let date = formatter.date(from: trimmed) { return date }
        }
        return nil
    }
}

public enum KeychainStore {
    private static let service = "os.helikon.companion"

    public static func set(_ value: String?, account: String) {
        guard let data = value?.data(using: .utf8) else {
            delete(account: account)
            return
        }
        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecAttrAccount as String: account
        ]
        let attributes: [String: Any] = [kSecValueData as String: data]
        let status = SecItemUpdate(query as CFDictionary, attributes as CFDictionary)
        if status == errSecItemNotFound {
            var add = query
            add[kSecValueData as String] = data
            SecItemAdd(add as CFDictionary, nil)
        }
    }

    public static func get(account: String) -> String? {
        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecAttrAccount as String: account,
            kSecReturnData as String: true,
            kSecMatchLimit as String: kSecMatchLimitOne
        ]
        var result: CFTypeRef?
        guard SecItemCopyMatching(query as CFDictionary, &result) == errSecSuccess,
              let data = result as? Data else { return nil }
        return String(data: data, encoding: .utf8)
    }

    public static func delete(account: String) {
        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecAttrAccount as String: account
        ]
        SecItemDelete(query as CFDictionary)
    }

    public static let lastFMSecretAccount = "lastfm.api.secret"
    public static let lastFMSessionAccount = "lastfm.session.key"
}

private extension Array {
    func chunked(into size: Int) -> [[Element]] {
        guard size > 0 else { return [self] }
        return stride(from: 0, to: count, by: size).map {
            Array(self[$0..<Swift.min($0 + size, count)])
        }
    }
}
