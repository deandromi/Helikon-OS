import XCTest
@testable import CompanionCore

final class TransferPlannerTests: XCTestCase {
    private var root: URL!

    override func setUpWithError() throws {
        root = FileManager.default.temporaryDirectory
            .appendingPathComponent("HelikonCompanionTests-\(UUID().uuidString)", isDirectory: true)
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
    }

    override func tearDownWithError() throws {
        try? FileManager.default.removeItem(at: root)
    }

    func testPlanDetectsConflictAndKeepBothRenames() async throws {
        let sourceFolder = root.appendingPathComponent("source", isDirectory: true)
        let destination = root.appendingPathComponent("destination", isDirectory: true)
        try FileManager.default.createDirectory(at: sourceFolder, withIntermediateDirectories: true)
        try FileManager.default.createDirectory(at: destination, withIntermediateDirectories: true)
        let source = sourceFolder.appendingPathComponent("track.mp3")
        try Data("audio".utf8).write(to: source)
        try Data("existing".utf8).write(to: destination.appendingPathComponent("track.mp3"))

        let service = TransferService(backupsRoot: root.appendingPathComponent("backups", isDirectory: true))
        let plan = try await service.plan(
            files: [source],
            destinationRoot: destination,
            relativeRoot: sourceFolder,
            kind: .music,
            conflictPolicy: .replaceWithBackup
        )
        XCTAssertEqual(plan.items.count, 1)
        XCTAssertTrue(plan.items[0].conflict)
        XCTAssertEqual(plan.items[0].relativePath, "track.mp3")
        XCTAssertEqual(plan.items[0].byteCount, 5)

        let keepBoth = try await service.plan(
            files: [source],
            destinationRoot: destination,
            relativeRoot: sourceFolder,
            kind: .music,
            conflictPolicy: .keepBoth
        )
        XCTAssertFalse(keepBoth.items[0].conflict)
        XCTAssertEqual(keepBoth.items[0].destinationURL.lastPathComponent, "track 2.mp3")
    }

    func testDuplicatePlanDestinationsAreMadeUnique() async throws {
        let firstFolder = root.appendingPathComponent("one", isDirectory: true)
        let secondFolder = root.appendingPathComponent("two", isDirectory: true)
        let destination = root.appendingPathComponent("destination", isDirectory: true)
        try FileManager.default.createDirectory(at: firstFolder, withIntermediateDirectories: true)
        try FileManager.default.createDirectory(at: secondFolder, withIntermediateDirectories: true)
        try FileManager.default.createDirectory(at: destination, withIntermediateDirectories: true)
        let first = firstFolder.appendingPathComponent("track.mp3")
        let second = secondFolder.appendingPathComponent("track.mp3")
        try Data("one".utf8).write(to: first)
        try Data("two".utf8).write(to: second)

        let service = TransferService(backupsRoot: root.appendingPathComponent("backups", isDirectory: true))
        let plan = try await service.plan(
            files: [first, second],
            destinationRoot: destination,
            relativeRoot: nil,
            kind: .music,
            conflictPolicy: .replaceWithBackup
        )
        XCTAssertEqual(plan.items.count, 2)
        XCTAssertNotEqual(plan.items[0].destinationURL, plan.items[1].destinationURL)
        XCTAssertEqual(plan.items[1].destinationURL.lastPathComponent, "track 2.mp3")
    }

    func testExecuteCopiesVerifiesAndReports() async throws {
        let sourceFolder = root.appendingPathComponent("source", isDirectory: true)
        let destination = root.appendingPathComponent("destination", isDirectory: true)
        try FileManager.default.createDirectory(at: sourceFolder, withIntermediateDirectories: true)
        try FileManager.default.createDirectory(at: destination, withIntermediateDirectories: true)
        let source = sourceFolder.appendingPathComponent("track.mp3")
        let payload = Data(repeating: 0x5A, count: 128 * 1024)
        try payload.write(to: source)

        let service = TransferService(backupsRoot: root.appendingPathComponent("backups", isDirectory: true))
        let plan = try await service.plan(
            files: [source],
            destinationRoot: destination,
            relativeRoot: sourceFolder,
            kind: .music,
            conflictPolicy: .skip
        )
        let report = try await service.execute(plan: plan, verify: true)
        XCTAssertEqual(report.completed, 1)
        XCTAssertEqual(report.failed, 0)
        XCTAssertEqual(try Data(contentsOf: destination.appendingPathComponent("track.mp3")), payload)
    }

    func testExecuteReplacesWithVerifiedBackup() async throws {
        let sourceFolder = root.appendingPathComponent("source", isDirectory: true)
        let destination = root.appendingPathComponent("destination", isDirectory: true)
        try FileManager.default.createDirectory(at: sourceFolder, withIntermediateDirectories: true)
        try FileManager.default.createDirectory(at: destination, withIntermediateDirectories: true)
        let source = sourceFolder.appendingPathComponent("track.mp3")
        try Data("new audio".utf8).write(to: source)
        let existing = destination.appendingPathComponent("track.mp3")
        try Data("old audio".utf8).write(to: existing)

        let service = TransferService(backupsRoot: root.appendingPathComponent("backups", isDirectory: true))
        let plan = try await service.plan(
            files: [source],
            destinationRoot: destination,
            relativeRoot: sourceFolder,
            kind: .music,
            conflictPolicy: .replaceWithBackup
        )
        let report = try await service.execute(plan: plan, verify: true)
        XCTAssertEqual(report.completed, 1)
        XCTAssertNotNil(report.backupFolder)
        XCTAssertEqual(try Data(contentsOf: existing), Data("new audio".utf8))
        let backupRoot = try XCTUnwrap(report.backupFolder)
        XCTAssertEqual(try Data(contentsOf: backupRoot.appendingPathComponent("track.mp3")), Data("old audio".utf8))
    }

    func testExecuteWritesTagsToTheCopyOnly() async throws {
        let sourceFolder = root.appendingPathComponent("source", isDirectory: true)
        let destination = root.appendingPathComponent("destination", isDirectory: true)
        try FileManager.default.createDirectory(at: sourceFolder, withIntermediateDirectories: true)
        try FileManager.default.createDirectory(at: destination, withIntermediateDirectories: true)
        let source = sourceFolder.appendingPathComponent("track.mp3")
        let audio = Data([0xFF, 0xFB, 0x90, 0x64] + Array(repeating: 0x11, count: 4096))
        try audio.write(to: source)

        let service = TransferService(backupsRoot: root.appendingPathComponent("backups", isDirectory: true))
        let plan = try await service.plan(
            files: [source],
            destinationRoot: destination,
            relativeRoot: sourceFolder,
            kind: .music,
            conflictPolicy: .skip
        )
        let tags = ID3TagWriter.Tags(title: "Tagged", artist: "Aster Vale", album: "The Cartographer", year: "2025")
        let report = try await service.execute(
            plan: plan,
            verify: true,
            tagsBySource: [source: tags]
        )
        XCTAssertEqual(report.completed, 1)
        XCTAssertEqual(Array(try Data(contentsOf: source).prefix(3)), [0xFF, 0xFB, 0x90])
        let copied = try Data(contentsOf: destination.appendingPathComponent("track.mp3"))
        XCTAssertEqual(String(data: copied.prefix(3), encoding: .ascii), "ID3")
        XCTAssertTrue(copied.count > audio.count)
    }

    func testExecuteSkipsExistingWhenAsked() async throws {
        let sourceFolder = root.appendingPathComponent("source", isDirectory: true)
        let destination = root.appendingPathComponent("destination", isDirectory: true)
        try FileManager.default.createDirectory(at: sourceFolder, withIntermediateDirectories: true)
        try FileManager.default.createDirectory(at: destination, withIntermediateDirectories: true)
        let source = sourceFolder.appendingPathComponent("track.mp3")
        try Data("new".utf8).write(to: source)
        let existing = destination.appendingPathComponent("track.mp3")
        try Data("old".utf8).write(to: existing)

        let service = TransferService(backupsRoot: root.appendingPathComponent("backups", isDirectory: true))
        let plan = try await service.plan(
            files: [source],
            destinationRoot: destination,
            relativeRoot: sourceFolder,
            kind: .music,
            conflictPolicy: .skip
        )
        let report = try await service.execute(plan: plan, verify: false)
        XCTAssertEqual(report.skipped, 1)
        XCTAssertEqual(try Data(contentsOf: existing), Data("old".utf8))
    }
}
