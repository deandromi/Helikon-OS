import Foundation

/// Writes ID3v2.3 tags to MP3 files. It never rewrites audio bytes and is only
/// used on a copy before transfer; the caller owns backup and verification.
public enum ID3TagWriter {
    public struct Tags: Sendable {
        public var title: String?
        public var artist: String?
        public var album: String?
        public var albumArtist: String?
        public var year: String?
        public var genre: String?
        public var trackNumber: Int?
        public var discNumber: Int?
        public var artworkPath: String?

        public init(
            title: String? = nil,
            artist: String? = nil,
            album: String? = nil,
            albumArtist: String? = nil,
            year: String? = nil,
            genre: String? = nil,
            trackNumber: Int? = nil,
            discNumber: Int? = nil,
            artworkPath: String? = nil
        ) {
            self.title = title
            self.artist = artist
            self.album = album
            self.albumArtist = albumArtist
            self.year = year
            self.genre = genre
            self.trackNumber = trackNumber
            self.discNumber = discNumber
            self.artworkPath = artworkPath
        }

        public init(track: Track) {
            self.init(
                title: track.displayTitle,
                artist: track.displayArtist,
                album: track.displayAlbum,
                albumArtist: track.albumArtist,
                year: track.year,
                genre: track.genre,
                trackNumber: track.trackNumber,
                discNumber: track.discNumber,
                artworkPath: track.artworkPath
            )
        }
    }

    public static func write(_ tags: Tags, to url: URL) throws {
        guard url.pathExtension.lowercased() == "mp3" else { return }
        let oldTagSize = try existingTagSize(at: url)
        let tagData = makeTag(tags)
        let temporary = url.deletingLastPathComponent()
            .appendingPathComponent(".helikon-tag-\(UUID().uuidString)")

        FileManager.default.createFile(atPath: temporary.path, contents: nil)
        let output = try FileHandle(forWritingTo: temporary)
        let input = try FileHandle(forReadingFrom: url)
        defer { try? output.close(); try? input.close() }

        try output.write(contentsOf: tagData)
        if oldTagSize > 0 {
            try input.seek(toOffset: UInt64(oldTagSize))
        }
        while true {
            let chunk = try input.read(upToCount: 1024 * 1024) ?? Data()
            if chunk.isEmpty { break }
            try output.write(contentsOf: chunk)
        }
        try output.synchronize()

        do {
            _ = try FileManager.default.replaceItemAt(url, withItemAt: temporary)
        } catch {
            try? FileManager.default.removeItem(at: temporary)
            throw error
        }
    }

    public static func makeTag(_ tags: Tags) -> Data {
        var frames = Data()
        frames.append(textFrame("TIT2", tags.title))
        frames.append(textFrame("TPE1", tags.artist))
        frames.append(textFrame("TALB", tags.album))
        frames.append(textFrame("TPE2", tags.albumArtist))
        frames.append(textFrame("TYER", tags.year.map { String($0.prefix(4)) }))
        frames.append(textFrame("TCON", tags.genre))
        if let track = tags.trackNumber {
            frames.append(textFrame("TRCK", String(track)))
        }
        if let disc = tags.discNumber {
            frames.append(textFrame("TPOS", String(disc)))
        }
        if let artworkPath = tags.artworkPath,
           let artwork = try? Data(contentsOf: URL(fileURLWithPath: artworkPath)),
           artwork.count <= 6 * 1024 * 1024 {
            frames.append(attachedPictureFrame(artwork))
        }

        let padding = Data(repeating: 0, count: 1024)
        let size = frames.count + padding.count
        var tag = Data()
        tag.append(contentsOf: [0x49, 0x44, 0x33]) // ID3
        tag.append(contentsOf: [0x03, 0x00])       // v2.3.0
        tag.append(0x00)                            // flags
        tag.append(contentsOf: synchsafe(size))
        tag.append(frames)
        tag.append(padding)
        return tag
    }

    public static func existingTagSize(at url: URL) throws -> Int {
        let handle = try FileHandle(forReadingFrom: url)
        defer { try? handle.close() }
        let header = try handle.read(upToCount: 10) ?? Data()
        guard header.count == 10,
              header[0] == 0x49, header[1] == 0x44, header[2] == 0x33 else { return 0 }
        let flags = header[5]
        let size = Int(header[6] & 0x7F) << 21
            | Int(header[7] & 0x7F) << 14
            | Int(header[8] & 0x7F) << 7
            | Int(header[9] & 0x7F)
        let footer = (flags & 0x10) != 0 ? 10 : 0
        return 10 + size + footer
    }

    private static func textFrame(_ id: String, _ value: String?) -> Data {
        guard let value else { return Data() }
        let cleaned = value.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !cleaned.isEmpty else { return Data() }
        var payload = Data([0x01]) // UTF-16 with BOM
        payload.append(contentsOf: [0xFF, 0xFE])
        payload.append(cleaned.data(using: .utf16LittleEndian) ?? Data())
        payload.append(contentsOf: [0x00, 0x00])
        return frame(id: id, payload: payload)
    }

    private static func attachedPictureFrame(_ image: Data) -> Data {
        var payload = Data([0x00]) // ISO-8859-1 description
        let mime = image.starts(with: [0x89, 0x50, 0x4E, 0x47]) ? "image/png" : "image/jpeg"
        payload.append(mime.data(using: .isoLatin1) ?? Data())
        payload.append(0x00)
        payload.append(0x03) // front cover
        payload.append(0x00) // empty description
        payload.append(image)
        return frame(id: "APIC", payload: payload)
    }

    private static func frame(id: String, payload: Data) -> Data {
        guard payload.count <= Int(UInt32.max) else { return Data() }
        var frame = Data(id.utf8.prefix(4))
        let size = UInt32(payload.count).bigEndian
        withUnsafeBytes(of: size) { frame.append(contentsOf: $0) }
        frame.append(contentsOf: [0x00, 0x00])
        frame.append(payload)
        return frame
    }

    private static func synchsafe(_ value: Int) -> [UInt8] {
        [
            UInt8((value >> 21) & 0x7F),
            UInt8((value >> 14) & 0x7F),
            UInt8((value >> 7) & 0x7F),
            UInt8(value & 0x7F)
        ]
    }
}
