import CryptoKit
import Foundation

public enum Formatters {
    private static let byteFormatter: ByteCountFormatter = {
        let formatter = ByteCountFormatter()
        formatter.countStyle = .file
        formatter.allowedUnits = [.useKB, .useMB, .useGB]
        formatter.isAdaptive = true
        return formatter
    }()

    public static func bytes(_ value: Int64) -> String {
        byteFormatter.string(fromByteCount: max(0, value))
    }

    public static func duration(_ seconds: TimeInterval) -> String {
        guard seconds.isFinite, seconds > 0 else { return "—" }
        let total = Int(seconds.rounded())
        let hours = total / 3600
        let minutes = (total % 3600) / 60
        let secs = total % 60
        if hours > 0 {
            return String(format: "%d:%02d:%02d", hours, minutes, secs)
        }
        return String(format: "%d:%02d", minutes, secs)
    }

    public static func relativeDate(_ date: Date?) -> String {
        guard let date else { return "Never" }
        let formatter = RelativeDateTimeFormatter()
        formatter.unitsStyle = .short
        return formatter.localizedString(for: date, relativeTo: Date())
    }

    public static func shortDate(_ date: Date?) -> String {
        guard let date else { return "—" }
        let formatter = DateFormatter()
        formatter.dateStyle = .medium
        formatter.timeStyle = .none
        return formatter.string(from: date)
    }

    public static func time(_ date: Date?) -> String {
        guard let date else { return "—" }
        let formatter = DateFormatter()
        formatter.dateStyle = .medium
        formatter.timeStyle = .short
        return formatter.string(from: date)
    }

    public static func signedMB(_ value: Int64) -> String {
        let mb = Double(value) / 1_000_000
        return String(format: "%.1f MB", mb)
    }
}

public enum Hashing {
    public static func sha256(of url: URL) throws -> String {
        let handle = try FileHandle(forReadingFrom: url)
        defer { try? handle.close() }
        var hasher = SHA256()
        while autoreleasepool(invoking: {
            let data = try? handle.read(upToCount: 1024 * 1024)
            guard let data, !data.isEmpty else { return false }
            hasher.update(data: data)
            return true
        }) {}
        return hasher.finalize().map { String(format: "%02x", $0) }.joined()
    }

    public static func sha256(of data: Data) -> String {
        SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined()
    }

    public static func md5(_ string: String) -> String {
        Insecure.MD5.hash(data: Data(string.utf8)).map { String(format: "%02x", $0) }.joined()
    }

    public static func stableID(_ components: String...) -> String {
        sha256(of: Data(components.joined(separator: "\u{1F}").utf8)).prefix(24).description
    }
}

public enum FileNaming {
    public static let invalidCharacters = CharacterSet(charactersIn: "/:\\?%*|\"<>")

    public static func sanitized(_ name: String, fallback: String = "Untitled") -> String {
        let trimmed = name.trimmingCharacters(in: .whitespacesAndNewlines)
        let source = trimmed.isEmpty ? fallback : trimmed
        let cleaned = source
            .components(separatedBy: invalidCharacters)
            .joined(separator: "-")
            .replacingOccurrences(of: "\\s+", with: " ", options: .regularExpression)
            .trimmingCharacters(in: .whitespacesAndNewlines)
        let result = String(cleaned.prefix(180))
        return result.isEmpty ? fallback : result
    }

    public static func uniqueDestination(for url: URL, in directory: URL, fileExists: (URL) -> Bool = { FileManager.default.fileExists(atPath: $0.path) }) -> URL {
        let base = url.deletingPathExtension().lastPathComponent
        let ext = url.pathExtension
        var candidate = directory.appendingPathComponent(url.lastPathComponent)
        var index = 2
        while fileExists(candidate) {
            let suffix = " \(index)"
            let name = ext.isEmpty ? "\(base)\(suffix)" : "\(base)\(suffix).\(ext)"
            candidate = directory.appendingPathComponent(name)
            index += 1
        }
        return candidate
    }
}

public enum JSONStore {
    public static let encoder: JSONEncoder = {
        let encoder = JSONEncoder()
        encoder.dateEncodingStrategy = .iso8601
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        return encoder
    }()

    public static let decoder: JSONDecoder = {
        let decoder = JSONDecoder()
        decoder.dateDecodingStrategy = .iso8601
        return decoder
    }()

    public static func load<T: Decodable>(_ type: T.Type, from url: URL, defaultValue: T) -> T {
        guard let data = try? Data(contentsOf: url) else { return defaultValue }
        return (try? decoder.decode(type, from: data)) ?? defaultValue
    }

    public static func save<T: Encodable>(_ value: T, to url: URL) throws {
        try FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        let data = try encoder.encode(value)
        try data.write(to: url, options: [.atomic])
    }
}

public enum AppPaths {
    public static var supportDirectory: URL {
        let base = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first
            ?? FileManager.default.temporaryDirectory
        return base.appendingPathComponent("Helikon Companion", isDirectory: true)
    }

    public static var libraryURL: URL { supportDirectory.appendingPathComponent("library.json") }
    public static var settingsURL: URL { supportDirectory.appendingPathComponent("settings.json") }
    public static var artworkDirectory: URL { supportDirectory.appendingPathComponent("Artwork", isDirectory: true) }
    public static var downloadsDirectory: URL { supportDirectory.appendingPathComponent("Downloads", isDirectory: true) }
    public static var backupsDirectory: URL { supportDirectory.appendingPathComponent("Backups", isDirectory: true) }
    public static var logsDirectory: URL { supportDirectory.appendingPathComponent("Logs", isDirectory: true) }

    public static func prepare() throws {
        for directory in [supportDirectory, artworkDirectory, downloadsDirectory, backupsDirectory, logsDirectory] {
            try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        }
    }
}

public enum AudioFileTypes {
    public static let supportedExtensions: Set<String> = [
        "mp3", "m4a", "aac", "aiff", "aif", "wav", "flac", "ogg", "oga", "opus", "wv", "ape", "wma", "alac"
    ]

    public static func isAudio(_ url: URL) -> Bool {
        supportedExtensions.contains(url.pathExtension.lowercased())
    }
}

public extension Collection {
    subscript(safe index: Index) -> Element? {
        indices.contains(index) ? self[index] : nil
    }
}
