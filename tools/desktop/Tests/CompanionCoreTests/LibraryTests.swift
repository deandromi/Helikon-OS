import XCTest
@testable import CompanionCore

final class LibraryTests: XCTestCase {
    func testDemoLibraryShape() {
        let snapshot = DemoLibrary.snapshot()
        XCTAssertEqual(snapshot.tracks.count, 15)
        XCTAssertEqual(snapshot.albums.count, 4)
        XCTAssertEqual(snapshot.artists.count, 4)
        XCTAssertEqual(snapshot.tracksMissingMetadata.count, 1)
        XCTAssertEqual(snapshot.podcasts.count, 1)
        XCTAssertEqual(snapshot.audiobooks.count, 2)
    }

    func testMergerPreservesUserMetadataAndRemovesDeletedFilesUnderRoot() throws {
        let root = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString, isDirectory: true)
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: root) }

        let oldURL = root.appendingPathComponent("old.mp3")
        var old = Track(fileURL: oldURL, title: "Old", artist: "Artist", album: "Album", rating: 5)
        old.notes = "Keep me"
        var snapshot = LibrarySnapshot(tracks: [old])

        let newURL = root.appendingPathComponent("new.mp3")
        let new = Track(fileURL: newURL, title: "New", artist: "Artist", album: "Album", duration: 10)
        let result = LibraryMerger.merge(
            scan: LibraryScanResult(tracks: [new], scannedCount: 1),
            into: snapshot,
            root: root
        )
        XCTAssertEqual(result.tracks.count, 1)
        XCTAssertEqual(result.tracks[0].title, "New")
        snapshot = result
        XCTAssertFalse(snapshot.tracks.contains(where: { $0.fileURL == oldURL }))
    }

    func testMergerCanIndexOneFileWithoutPruningItsFolder() throws {
        let root = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString, isDirectory: true)
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: root) }

        let keptURL = root.appendingPathComponent("kept.mp3")
        let newURL = root.appendingPathComponent("new.mp3")
        let kept = Track(fileURL: keptURL, title: "Kept", artist: "Artist", album: "Album")
        let new = Track(fileURL: newURL, title: "New", artist: "Artist", album: "Album")
        let result = LibraryMerger.merge(
            scan: LibraryScanResult(tracks: [new], scannedCount: 1),
            into: LibrarySnapshot(tracks: [kept]),
            root: root,
            prune: false
        )
        XCTAssertEqual(Set(result.tracks.map(\.title)), Set(["Kept", "New"]))
    }

    func testApplyCandidateUpdatesTrackAndMarksSource() {
        let track = Track(fileURL: URL(fileURLWithPath: "/tmp/a.mp3"), title: "", artist: "", album: "")
        let candidate = MetadataCandidate(
            id: "candidate",
            source: .musicBrainz,
            entity: .track,
            title: "Title",
            subtitle: "Artist",
            patch: [.title: "Title", .artist: "Artist", .album: "Album", .year: "2025"]
        )
        let updated = LibraryMerger.apply(candidate, to: track)
        XCTAssertEqual(updated.title, "Title")
        XCTAssertEqual(updated.artist, "Artist")
        XCTAssertEqual(updated.album, "Album")
        XCTAssertEqual(updated.year, "2025")
        XCTAssertEqual(updated.metadataSource, "MusicBrainz")
        XCTAssertTrue(updated.fetchedFields.contains(.title))
    }

    func testScannerReadsAWAVDurationWithoutWriting() async throws {
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString, isDirectory: true)
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: directory) }
        let url = directory.appendingPathComponent("Aster Vale - Test Tone.wav")
        try makeWAV(seconds: 1).write(to: url)

        let scanner = LibraryScanner()
        let track = try await scanner.readTrack(at: url, relativeTo: directory)
        XCTAssertEqual(track.title, "Test Tone")
        XCTAssertEqual(track.artist, "Aster Vale")
        XCTAssertEqual(track.format, "WAV")
        XCTAssertEqual(track.duration, 1, accuracy: 0.05)
        XCTAssertGreaterThan(track.fileSize, 0)
    }

    private func makeWAV(seconds: Int) -> Data {
        let sampleRate: UInt32 = 44_100
        let channels: UInt16 = 1
        let bits: UInt16 = 16
        let sampleCount = Int(sampleRate) * seconds
        let dataSize = UInt32(sampleCount * Int(channels) * Int(bits / 8))
        var data = Data()
        data.append("RIFF".data(using: .ascii)!)
        data.append(littleEndian(UInt32(36) + dataSize))
        data.append("WAVEfmt ".data(using: .ascii)!)
        data.append(littleEndian(UInt32(16)))
        data.append(littleEndian(UInt16(1)))
        data.append(littleEndian(channels))
        data.append(littleEndian(sampleRate))
        data.append(littleEndian(sampleRate * UInt32(channels) * UInt32(bits / 8)))
        data.append(littleEndian(channels * (bits / 8)))
        data.append(littleEndian(bits))
        data.append("data".data(using: .ascii)!)
        data.append(littleEndian(dataSize))
        data.append(Data(repeating: 0, count: Int(dataSize)))
        return data
    }

    private func littleEndian<T: FixedWidthInteger>(_ value: T) -> Data {
        var value = value.littleEndian
        return Data(bytes: &value, count: MemoryLayout<T>.size)
    }

    func testFileNameParsing() {
        let parsed = LibraryScanner.parseFileName("03 - Aster Vale - Ink and Latitude")
        XCTAssertEqual(parsed.trackNumber, 3)
        XCTAssertEqual(parsed.artist, "Aster Vale")
        XCTAssertEqual(parsed.title, "Ink and Latitude")
    }

    func testFileNamingSanitizesAndKeepsExtension() {
        XCTAssertEqual(FileNaming.sanitized("A/B: C?"), "A-B- C-")
        let directory = URL(fileURLWithPath: "/tmp")
        let candidate = FileNaming.uniqueDestination(
            for: URL(fileURLWithPath: "/tmp/song.mp3"),
            in: directory,
            fileExists: { $0.lastPathComponent != "song 2.mp3" }
        )
        XCTAssertEqual(candidate.lastPathComponent, "song 2.mp3")
    }
}

final class ID3TagWriterTests: XCTestCase {
    func testTagHeaderAndSizeAreWritten() {
        let tag = ID3TagWriter.makeTag(ID3TagWriter.Tags(
            title: "Title",
            artist: "Artist",
            album: "Album",
            year: "2025"
        ))
        XCTAssertEqual(String(data: tag.prefix(3), encoding: .ascii), "ID3")
        XCTAssertEqual(tag[3], 3)
        XCTAssertEqual(tag[4], 0)
        let size = Int(tag[6] & 0x7F) << 21
            | Int(tag[7] & 0x7F) << 14
            | Int(tag[8] & 0x7F) << 7
            | Int(tag[9] & 0x7F)
        XCTAssertEqual(size, tag.count - 10)
    }

    func testExistingTagSize() throws {
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString, isDirectory: true)
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: directory) }
        let url = directory.appendingPathComponent("sample.mp3")
        var data = ID3TagWriter.makeTag(ID3TagWriter.Tags(title: "A"))
        data.append(contentsOf: [0xFF, 0xFB, 0x00, 0x00])
        try data.write(to: url)
        let size = try ID3TagWriter.existingTagSize(at: url)
        XCTAssertGreaterThan(size, 10)
        XCTAssertLessThan(size, data.count)
    }
}
