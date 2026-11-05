import AVFoundation
import Foundation

public struct LibraryScanResult: Sendable {
    public var tracks: [Track]
    public var errors: [String]
    public var scannedCount: Int

    public init(tracks: [Track] = [], errors: [String] = [], scannedCount: Int = 0) {
        self.tracks = tracks
        self.errors = errors
        self.scannedCount = scannedCount
    }
}

/// Reads tags through AVFoundation and keeps the audio file untouched.
public final class LibraryScanner {
    private let fileManager = FileManager.default

    public init() {}

    public func scan(
        root: URL,
        progress: (@Sendable (Int, Int) -> Void)? = nil
    ) async -> LibraryScanResult {
        let files = audioFiles(in: root)
        var tracks: [Track] = []
        var errors: [String] = []
        var completed = 0

        for url in files {
            do {
                let track = try await readTrack(at: url, relativeTo: root)
                tracks.append(track)
            } catch {
                errors.append("\(url.lastPathComponent): \(error.localizedDescription)")
            }
            completed += 1
            progress?(completed, files.count)
            await Task.yield()
        }

        return LibraryScanResult(
            tracks: tracks.sorted { lhs, rhs in
                if lhs.album != rhs.album {
                    return lhs.album.localizedCaseInsensitiveCompare(rhs.album) == .orderedAscending
                }
                if lhs.discNumber ?? 0 != rhs.discNumber ?? 0 {
                    return (lhs.discNumber ?? 0) < (rhs.discNumber ?? 0)
                }
                return (lhs.trackNumber ?? 0) < (rhs.trackNumber ?? 0)
            },
            errors: errors,
            scannedCount: files.count
        )
    }

    public func readTrack(at url: URL, relativeTo root: URL? = nil) async throws -> Track {
        let values = try url.resourceValues(forKeys: [.fileSizeKey, .contentModificationDateKey, .creationDateKey])
        let fileSize = Int64(values.fileSize ?? 0)
        let modified = values.contentModificationDate ?? Date()
        let added = values.creationDate ?? modified

        let asset = AVURLAsset(url: url)
        let duration = (try? await asset.load(.duration)).map { CMTimeGetSeconds($0) } ?? 0
        let common = (try? await asset.load(.commonMetadata)) ?? []

        let title = await string(in: common, identifiers: [
            .commonIdentifierTitle,
            .iTunesMetadataSongName
        ])
        let artist = await string(in: common, identifiers: [
            .commonIdentifierArtist,
            .iTunesMetadataArtist,
            .iTunesMetadataOriginalArtist
        ])
        let album = await string(in: common, identifiers: [
            .commonIdentifierAlbumName,
            .iTunesMetadataAlbum
        ])
        let albumArtist = await string(in: common, identifiers: [
            .iTunesMetadataAlbumArtist
        ])
        let year = await string(in: common, identifiers: [
            .commonIdentifierCreationDate,
            .iTunesMetadataReleaseDate
        ])
        let genre = await string(in: common, identifiers: [
            .commonIdentifierType,
            .iTunesMetadataPredefinedGenre,
            .iTunesMetadataUserGenre
        ])
        let trackNumber = await number(in: common, identifiers: [.iTunesMetadataTrackNumber])
        let discNumber = await number(in: common, identifiers: [.iTunesMetadataDiscNumber])

        let artworkData = await data(in: common, identifiers: [
            .commonIdentifierArtwork,
            .iTunesMetadataCoverArt
        ])
        let artworkPath = artworkData.flatMap { try? saveArtwork($0) }

        let parsed = Self.parseFileName(url.deletingPathExtension().lastPathComponent)
        let finalTitle = cleaner(title) ?? parsed.title
        let finalArtist = cleaner(artist) ?? parsed.artist ?? ""
        let finalAlbum = cleaner(album) ?? "Unknown album"

        return Track(
            fileURL: url,
            relativePath: relativePath(for: url, root: root),
            title: finalTitle,
            artist: finalArtist,
            album: finalAlbum,
            albumArtist: cleaner(albumArtist) ?? "",
            trackNumber: trackNumber ?? parsed.trackNumber,
            discNumber: discNumber,
            year: cleaner(year),
            genre: cleaner(genre),
            duration: duration.isFinite ? max(0, duration) : 0,
            fileSize: fileSize,
            format: url.pathExtension.uppercased(),
            artworkPath: artworkPath,
            hasEmbeddedArtwork: artworkData != nil,
            addedAt: added,
            modifiedAt: modified
        )
    }

    public func audioFiles(in root: URL) -> [URL] {
        let skipped: Set<String> = [
            "iPod_Control", ".rockbox", ".rockpod-rw", ".Trashes", ".Spotlight-V100",
            ".fseventsd", ".TemporaryItems", "System Volume Information", ".helikon"
        ]
        var result: [URL] = []
        if AudioFileTypes.isAudio(root) {
            return [root]
        }
        guard let enumerator = fileManager.enumerator(
            at: root,
            includingPropertiesForKeys: [.isRegularFileKey, .isDirectoryKey, .isHiddenKey],
            options: [.skipsPackageDescendants],
            errorHandler: { _, _ in true }
        ) else { return [] }

        for case let url as URL in enumerator {
            let values = try? url.resourceValues(forKeys: [.isRegularFileKey, .isHiddenKey])
            if values?.isHidden == true { continue }
            if values?.isRegularFile == true, AudioFileTypes.isAudio(url) {
                result.append(url)
            } else if values?.isRegularFile == false {
                if skipped.contains(url.lastPathComponent) {
                    enumerator.skipDescendants()
                }
            }
        }
        return result.sorted { $0.path.localizedCaseInsensitiveCompare($1.path) == .orderedAscending }
    }

    private func relativePath(for url: URL, root: URL?) -> String? {
        guard let root else { return nil }
        let rootPath = root.standardizedFileURL.path
        let path = url.standardizedFileURL.path
        guard path.hasPrefix(rootPath) else { return url.lastPathComponent }
        var relative = String(path.dropFirst(rootPath.count))
        while relative.hasPrefix("/") { relative.removeFirst() }
        return relative
    }

    private func string(in items: [AVMetadataItem], identifiers: [AVMetadataIdentifier]) async -> String? {
        for identifier in identifiers {
            for item in AVMetadataItem.metadataItems(from: items, filteredByIdentifier: identifier) {
                if let value = try? await item.load(.stringValue), let cleaned = cleaner(value) {
                    return cleaned
                }
            }
        }
        return nil
    }

    private func number(in items: [AVMetadataItem], identifiers: [AVMetadataIdentifier]) async -> Int? {
        for identifier in identifiers {
            for item in AVMetadataItem.metadataItems(from: items, filteredByIdentifier: identifier) {
                if let value = try? await item.load(.numberValue) {
                    return value.intValue
                }
                if let value = try? await item.load(.stringValue),
                   let parsed = Int(value.components(separatedBy: CharacterSet.decimalDigits.inverted).first ?? "") {
                    return parsed
                }
            }
        }
        return nil
    }

    private func data(in items: [AVMetadataItem], identifiers: [AVMetadataIdentifier]) async -> Data? {
        for identifier in identifiers {
            for item in AVMetadataItem.metadataItems(from: items, filteredByIdentifier: identifier) {
                if let value = try? await item.load(.dataValue), !value.isEmpty {
                    return value
                }
            }
        }
        return nil
    }

    private func saveArtwork(_ data: Data) throws -> String {
        try fileManager.createDirectory(at: AppPaths.artworkDirectory, withIntermediateDirectories: true)
        let hash = Hashing.sha256(of: data)
        let ext: String
        if data.starts(with: [0xFF, 0xD8]) {
            ext = "jpg"
        } else if data.starts(with: [0x89, 0x50, 0x4E, 0x47]) {
            ext = "png"
        } else {
            ext = "art"
        }
        let url = AppPaths.artworkDirectory.appendingPathComponent("\(hash).\(ext)")
        if !fileManager.fileExists(atPath: url.path) {
            try data.write(to: url, options: [.atomic])
        }
        return url.path
    }

    private func cleaner(_ value: String?) -> String? {
        guard let value else { return nil }
        let cleaned = value
            .replacingOccurrences(of: "\u{0000}", with: "")
            .trimmingCharacters(in: .whitespacesAndNewlines)
        return cleaned.isEmpty ? nil : cleaned
    }

    public static func parseFileName(_ name: String) -> (title: String, artist: String?, trackNumber: Int?) {
        var working = name.trimmingCharacters(in: .whitespacesAndNewlines)
        var trackNumber: Int?
        if let match = working.range(of: #"^(\d{1,3})[\s\.\-_]+"#, options: .regularExpression) {
            let prefix = String(working[match]).trimmingCharacters(in: CharacterSet(charactersIn: " .-_"))
            trackNumber = Int(prefix)
            working.removeSubrange(match)
        }
        if let range = working.range(of: " - ", options: .backwards) {
            let artist = String(working[..<range.lowerBound]).trimmingCharacters(in: .whitespaces)
            let title = String(working[range.upperBound...]).trimmingCharacters(in: .whitespaces)
            if !artist.isEmpty && !title.isEmpty {
                return (title, artist, trackNumber)
            }
        }
        return (working.isEmpty ? name : working, nil, trackNumber)
    }
}
