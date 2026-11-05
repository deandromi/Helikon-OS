import Foundation

/// Finds mounted HFS+ volumes that could be an iPod or a Helikon-OS test image.
/// It reads metadata only; it never mounts, repairs, formats or writes a volume.
public enum DeviceDetector {
    public static func detectDevices() -> [DeviceInfo] {
        let keys: [URLResourceKey] = [
            .volumeNameKey,
            .volumeTotalCapacityKey,
            .volumeAvailableCapacityKey,
            .volumeIsReadOnlyKey,
            .volumeIsLocalKey,
            .volumeIsRemovableKey,
            .volumeURLForRemountingKey
        ]
        let urls = FileManager.default.mountedVolumeURLs(
            includingResourceValuesForKeys: keys,
            options: [.skipHiddenVolumes]
        ) ?? []

        return urls.compactMap { url in
            guard url.path != "/" else { return nil }
            return inspectVolume(at: url)
        }
        .sorted { lhs, rhs in
            if lhs.hasHelikonFolder != rhs.hasHelikonFolder { return lhs.hasHelikonFolder }
            if lhs.hasIPodControl != rhs.hasIPodControl { return lhs.hasIPodControl }
            if lhs.hasRockboxFolder != rhs.hasRockboxFolder { return lhs.hasRockboxFolder }
            if lhs.isRemovable != rhs.isRemovable { return lhs.isRemovable }
            return lhs.name.localizedCaseInsensitiveCompare(rhs.name) == .orderedAscending
        }
    }

    public static func inspectVolume(at url: URL) -> DeviceInfo? {
        let info = diskutilInfo(at: url)
        let filesystem = info["FilesystemType"] as? String
            ?? info["FilesystemName"] as? String
            ?? ""
        let isHFS = filesystem.lowercased() == "hfs" || filesystem.lowercased() == "hfsx"
        guard isHFS else { return nil }

        let values = try? url.resourceValues(forKeys: [
            .volumeNameKey,
            .volumeTotalCapacityKey,
            .volumeAvailableCapacityKey,
            .volumeIsReadOnlyKey,
            .volumeIsRemovableKey
        ])

        let total = int64(info["TotalSize"]) ?? Int64(values?.volumeTotalCapacity ?? 0)
        let free = int64(info["FreeSpace"]) ?? Int64(values?.volumeAvailableCapacity ?? 0)
        let readOnly = (info["Writable"] as? Bool).map { !$0 }
            ?? values?.volumeIsReadOnly
            ?? false
        let removable = (info["RemovableMedia"] as? Bool)
            ?? values?.volumeIsRemovable
            ?? false
        let name = (info["VolumeName"] as? String)
            ?? values?.volumeName
            ?? url.lastPathComponent
        let hasHelikon = FileManager.default.fileExists(atPath: url.appendingPathComponent(".helikon").path)
        let hasIPod = FileManager.default.fileExists(atPath: url.appendingPathComponent("iPod_Control").path)
        let hasRockbox = FileManager.default.fileExists(atPath: url.appendingPathComponent(".rockbox").path)

        return DeviceInfo(
            id: url.path,
            name: name.isEmpty ? url.lastPathComponent : name,
            mountURL: url,
            filesystem: filesystem.isEmpty ? "HFS+" : filesystem.uppercased(),
            isHFS: isHFS,
            isReadOnly: readOnly,
            isRemovable: removable,
            totalCapacity: total,
            availableCapacity: free,
            hasHelikonFolder: hasHelikon,
            hasIPodControl: hasIPod,
            hasRockboxFolder: hasRockbox,
            model: nil,
            busProtocol: info["BusProtocol"] as? String
        )
    }

    private static func diskutilInfo(at url: URL) -> [String: Any] {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/usr/sbin/diskutil")
        process.arguments = ["info", "-plist", url.path]
        let pipe = Pipe()
        process.standardOutput = pipe
        process.standardError = FileHandle.nullDevice
        do {
            try process.run()
            process.waitUntilExit()
            guard process.terminationStatus == 0 else { return [:] }
            let data = pipe.fileHandleForReading.readDataToEndOfFile()
            return (try PropertyListSerialization.propertyList(from: data, options: [], format: nil) as? [String: Any]) ?? [:]
        } catch {
            return [:]
        }
    }

    private static func int64(_ value: Any?) -> Int64? {
        switch value {
        case let value as Int64: return value
        case let value as Int: return Int64(value)
        case let value as NSNumber: return value.int64Value
        case let value as String: return Int64(value)
        default: return nil
        }
    }
}
