import CryptoKit
import Foundation

public enum TransferError: LocalizedError {
    case destinationNotWritable(String)
    case sourceMissing(String)
    case outsideDestination(String)
    case verificationFailed(String)
    case backupFailed(String)

    public var errorDescription: String? {
        switch self {
        case .destinationNotWritable(let path): return "The destination is not writable: \(path)"
        case .sourceMissing(let path): return "The source file is missing: \(path)"
        case .outsideDestination(let path): return "The destination is outside the selected folder: \(path)"
        case .verificationFailed(let path): return "Checksum verification failed for \(path)."
        case .backupFailed(let path): return "Could not back up \(path)."
        }
    }
}

public actor TransferService {
    private let fileManager = FileManager.default
    private let backupsRoot: URL

    public init(backupsRoot: URL = AppPaths.backupsDirectory) {
        self.backupsRoot = backupsRoot
    }

    public func plan(
        files: [URL],
        destinationRoot: URL,
        relativeRoot: URL? = nil,
        kind: MediaKind,
        conflictPolicy: TransferConflictPolicy
    ) throws -> TransferPlan {
        guard isWritable(destinationRoot) else {
            throw TransferError.destinationNotWritable(destinationRoot.path)
        }

        var items: [TransferItem] = []
        for source in files {
            let values = try? source.resourceValues(forKeys: [.fileSizeKey, .isRegularFileKey])
            guard values?.isRegularFile == true else {
                throw TransferError.sourceMissing(source.path)
            }
            let relative = relativePath(for: source, root: relativeRoot)
            var destination = destinationRoot.appendingPathComponent(relative)
            var conflict = fileManager.fileExists(atPath: destination.path)
            if conflict && conflictPolicy == .keepBoth {
                destination = FileNaming.uniqueDestination(for: source, in: destination.deletingLastPathComponent())
                conflict = false
            }
            items.append(TransferItem(
                sourceURL: source,
                destinationURL: destination,
                relativePath: relative,
                byteCount: Int64(values?.fileSize ?? 0),
                kind: kind,
                conflict: conflict
            ))
        }
        return TransferPlan(
            destinationRoot: destinationRoot,
            items: items,
            conflictPolicy: conflictPolicy
        )
    }

    public func execute(
        plan: TransferPlan,
        verify: Bool = true,
        tagsBySource: [URL: ID3TagWriter.Tags] = [:],
        onProgress: (@Sendable (TransferItem) -> Void)? = nil
    ) async throws -> TransferReport {
        var report = TransferReport()
        let backupRoot = backupsRoot
            .appendingPathComponent(FileNaming.sanitized(plan.deviceName ?? "Local"), isDirectory: true)
            .appendingPathComponent(Self.timestampFolder(), isDirectory: true)

        for var item in plan.items {
            if item.conflict && plan.conflictPolicy == .skip {
                item.status = .skipped
                report.skipped += 1
                onProgress?(item)
                continue
            }

            do {
                guard fileManager.fileExists(atPath: item.sourceURL.path) else {
                    throw TransferError.sourceMissing(item.sourceURL.path)
                }
                try ensureInsideRoot(item.destinationURL, root: plan.destinationRoot)
                try fileManager.createDirectory(
                    at: item.destinationURL.deletingLastPathComponent(),
                    withIntermediateDirectories: true
                )

                if item.conflict && plan.conflictPolicy == .replaceWithBackup {
                    let backup = try backupExisting(item: item, root: backupRoot)
                    item.backupURL = backup
                }

                let ext = item.sourceURL.pathExtension
                let temporaryName = ".helikon-partial-\(UUID().uuidString)" + (ext.isEmpty ? "" : ".\(ext)")
                let temporary = item.destinationURL.deletingLastPathComponent()
                    .appendingPathComponent(temporaryName)
                defer { try? fileManager.removeItem(at: temporary) }
                let sourceHash = try await copyStreaming(
                    from: item.sourceURL,
                    to: temporary,
                    item: &item,
                    onProgress: onProgress
                )
                item.status = .verifying
                item.progress = 0.94
                onProgress?(item)

                if verify {
                    let copiedHash = try Hashing.sha256(of: temporary)
                    guard copiedHash == sourceHash else {
                        throw TransferError.verificationFailed(item.destinationURL.lastPathComponent)
                    }
                }

                var finalHash = sourceHash
                if let tags = tagsBySource[item.sourceURL] {
                    try ID3TagWriter.write(tags, to: temporary)
                    finalHash = try Hashing.sha256(of: temporary)
                }

                if fileManager.fileExists(atPath: item.destinationURL.path) {
                    _ = try fileManager.replaceItemAt(item.destinationURL, withItemAt: temporary)
                } else {
                    try fileManager.moveItem(at: temporary, to: item.destinationURL)
                }
                if verify {
                    let destinationHash = try Hashing.sha256(of: item.destinationURL)
                    guard destinationHash == finalHash else {
                        throw TransferError.verificationFailed(item.destinationURL.lastPathComponent)
                    }
                }
                item.checksum = finalHash
                item.status = .complete
                item.progress = 1
                report.completed += 1
                report.bytesTransferred += item.byteCount
                if report.backupFolder == nil, item.backupURL != nil {
                    report.backupFolder = backupRoot
                }
                onProgress?(item)
            } catch {
                item.status = .failed
                item.error = error.localizedDescription
                item.progress = 0
                report.failed += 1
                report.errors.append("\(item.sourceURL.lastPathComponent): \(error.localizedDescription)")
                onProgress?(item)
            }
        }
        return report
    }

    private func copyStreaming(
        from source: URL,
        to destination: URL,
        item: inout TransferItem,
        onProgress: (@Sendable (TransferItem) -> Void)?
    ) async throws -> String {
        guard fileManager.createFile(atPath: destination.path, contents: nil) else {
            throw TransferError.destinationNotWritable(destination.path)
        }
        let input = try FileHandle(forReadingFrom: source)
        let output = try FileHandle(forWritingTo: destination)
        defer { try? input.close(); try? output.close() }

        var hasher = SHA256()
        var written: Int64 = 0
        let total = max(1, item.byteCount)
        item.status = .copying
        item.progress = 0
        onProgress?(item)

        while true {
            let data = try input.read(upToCount: 1024 * 1024) ?? Data()
            if data.isEmpty { break }
            hasher.update(data: data)
            try output.write(contentsOf: data)
            written += Int64(data.count)
            item.progress = min(0.92, Double(written) / Double(total) * 0.9)
            onProgress?(item)
            await Task.yield()
        }
        try output.synchronize()
        return hasher.finalize().map { String(format: "%02x", $0) }.joined()
    }

    private func backupExisting(item: TransferItem, root: URL) throws -> URL {
        let relative = item.relativePath
        let backup = root.appendingPathComponent(relative)
        try fileManager.createDirectory(at: backup.deletingLastPathComponent(), withIntermediateDirectories: true)
        do {
            try fileManager.copyItem(at: item.destinationURL, to: backup)
            let sourceHash = try Hashing.sha256(of: item.destinationURL)
            let backupHash = try Hashing.sha256(of: backup)
            guard sourceHash == backupHash else {
                throw TransferError.backupFailed(item.destinationURL.path)
            }
            return backup
        } catch {
            throw TransferError.backupFailed(item.destinationURL.path)
        }
    }

    private func relativePath(for source: URL, root: URL?) -> String {
        guard let root else { return source.lastPathComponent }
        let rootPath = root.standardizedFileURL.path
        let path = source.standardizedFileURL.path
        guard path.hasPrefix(rootPath) else { return source.lastPathComponent }
        var relative = String(path.dropFirst(rootPath.count))
        while relative.hasPrefix("/") { relative.removeFirst() }
        return relative.isEmpty ? source.lastPathComponent : relative
    }

    private func ensureInsideRoot(_ destination: URL, root: URL) throws {
        let rootPath = root.standardizedFileURL.path
        let destinationPath = destination.standardizedFileURL.path
        guard destinationPath == rootPath || destinationPath.hasPrefix(rootPath + "/") else {
            throw TransferError.outsideDestination(destination.path)
        }
    }

    private func isWritable(_ url: URL) -> Bool {
        var isDirectory: ObjCBool = false
        if fileManager.fileExists(atPath: url.path, isDirectory: &isDirectory) {
            return isDirectory.boolValue && fileManager.isWritableFile(atPath: url.path)
        }
        var ancestor = url.deletingLastPathComponent()
        while !fileManager.fileExists(atPath: ancestor.path) {
            let parent = ancestor.deletingLastPathComponent()
            if parent.path == ancestor.path { return false }
            ancestor = parent
        }
        return fileManager.isWritableFile(atPath: ancestor.path)
    }

    private static func timestampFolder() -> String {
        let formatter = DateFormatter()
        formatter.locale = Locale(identifier: "en_US_POSIX")
        formatter.dateFormat = "yyyy-MM-dd HH.mm.ss"
        return formatter.string(from: Date())
    }
}

public struct BackupManifest: Codable, Hashable, Sendable {
    public var createdAt: Date
    public var label: String
    public var root: String
    public var files: [BackupFile]

    public init(createdAt: Date = Date(), label: String, root: String, files: [BackupFile]) {
        self.createdAt = createdAt
        self.label = label
        self.root = root
        self.files = files
    }
}

public struct BackupFile: Codable, Hashable, Sendable {
    public var relativePath: String
    public var byteCount: Int64
    public var checksum: String
}

public struct BackupService {
    public init() {}

    public func backup(
        urls: [URL],
        label: String,
        progress: (@Sendable (Int, Int, String) -> Void)? = nil
    ) async throws -> URL {
        let folder = AppPaths.backupsDirectory
            .appendingPathComponent("\(FileNaming.sanitized(label)) \(Self.timestampFolder())", isDirectory: true)
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)

        var sources: [(source: URL, base: URL)] = []
        for url in urls {
            for source in regularFiles(in: url) {
                sources.append((source, url))
            }
        }

        var files: [BackupFile] = []
        for (index, entry) in sources.enumerated() {
            let basePath = entry.base.deletingLastPathComponent().path + "/"
            let relative = entry.source.path.replacingOccurrences(of: basePath, with: "")
            let destination = folder.appendingPathComponent(relative)
            try FileManager.default.createDirectory(at: destination.deletingLastPathComponent(), withIntermediateDirectories: true)
            try FileManager.default.copyItem(at: entry.source, to: destination)
            let checksum = try Hashing.sha256(of: destination)
            let size = Int64((try? entry.source.resourceValues(forKeys: [.fileSizeKey]).fileSize) ?? 0)
            files.append(BackupFile(relativePath: relative, byteCount: size, checksum: checksum))
            progress?(index + 1, sources.count, entry.source.lastPathComponent)
        }

        let manifest = BackupManifest(label: label, root: folder.path, files: files)
        try JSONStore.save(manifest, to: folder.appendingPathComponent("manifest.json"))
        return folder
    }

    private func regularFiles(in url: URL) -> [URL] {
        if (try? url.resourceValues(forKeys: [.isRegularFileKey]).isRegularFile) == true {
            return [url]
        }
        guard let enumerator = FileManager.default.enumerator(
            at: url,
            includingPropertiesForKeys: [.isRegularFileKey, .fileSizeKey],
            options: [.skipsHiddenFiles, .skipsPackageDescendants]
        ) else { return [] }
        var result: [URL] = []
        for case let child as URL in enumerator {
            if (try? child.resourceValues(forKeys: [.isRegularFileKey]).isRegularFile) == true {
                result.append(child)
            }
        }
        return result
    }

    private static func timestampFolder() -> String {
        let formatter = DateFormatter()
        formatter.locale = Locale(identifier: "en_US_POSIX")
        formatter.dateFormat = "yyyy-MM-dd HH.mm.ss"
        return formatter.string(from: Date())
    }
}
