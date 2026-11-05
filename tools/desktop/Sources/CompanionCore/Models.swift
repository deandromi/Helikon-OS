import Foundation

// MARK: - Media

public enum MediaKind: String, Codable, CaseIterable, Hashable, Sendable {
    case music
    case podcast
    case audiobook
    case artwork
    case other

    public var title: String {
        switch self {
        case .music: return "Music"
        case .podcast: return "Podcast"
        case .audiobook: return "Audiobook"
        case .artwork: return "Artwork"
        case .other: return "File"
        }
    }
}

public enum MetadataField: String, Codable, CaseIterable, Hashable, Sendable {
    case title
    case artist
    case album
    case albumArtist
    case year
    case genre
    case artwork
    case description
    case biography

    public var title: String {
        switch self {
        case .title: return "Title"
        case .artist: return "Artist"
        case .album: return "Album"
        case .albumArtist: return "Album artist"
        case .year: return "Year"
        case .genre: return "Genre"
        case .artwork: return "Artwork"
        case .description: return "Description"
        case .biography: return "Biography"
        }
    }
}

public enum MetadataProvider: String, Codable, CaseIterable, Hashable, Sendable, Identifiable {
    case musicBrainz
    case iTunes
    case openLibrary
    case librivox
    case lastFM

    public var id: String { rawValue }

    public var title: String {
        switch self {
        case .musicBrainz: return "MusicBrainz"
        case .iTunes: return "Apple"
        case .openLibrary: return "Open Library"
        case .librivox: return "LibriVox"
        case .lastFM: return "Last.fm"
        }
    }
}

public struct Track: Identifiable, Codable, Hashable, Sendable {
    public var id: UUID
    public var fileURL: URL
    public var relativePath: String?
    public var title: String
    public var artist: String
    public var album: String
    public var albumArtist: String
    public var trackNumber: Int?
    public var discNumber: Int?
    public var year: String?
    public var genre: String?
    public var duration: TimeInterval
    public var fileSize: Int64
    public var format: String
    public var artworkPath: String?
    public var hasEmbeddedArtwork: Bool
    public var addedAt: Date
    public var modifiedAt: Date
    public var rating: Int
    public var playCount: Int
    public var lastPlayed: Date?
    public var metadataSource: String?
    public var fetchedFields: Set<MetadataField>
    public var notes: String?
    public var isDemo: Bool

    public init(
        id: UUID = UUID(),
        fileURL: URL,
        relativePath: String? = nil,
        title: String,
        artist: String,
        album: String,
        albumArtist: String = "",
        trackNumber: Int? = nil,
        discNumber: Int? = nil,
        year: String? = nil,
        genre: String? = nil,
        duration: TimeInterval = 0,
        fileSize: Int64 = 0,
        format: String = "",
        artworkPath: String? = nil,
        hasEmbeddedArtwork: Bool = false,
        addedAt: Date = Date(),
        modifiedAt: Date = Date(),
        rating: Int = 0,
        playCount: Int = 0,
        lastPlayed: Date? = nil,
        metadataSource: String? = nil,
        fetchedFields: Set<MetadataField> = [],
        notes: String? = nil,
        isDemo: Bool = false
    ) {
        self.id = id
        self.fileURL = fileURL
        self.relativePath = relativePath
        self.title = title
        self.artist = artist
        self.album = album
        self.albumArtist = albumArtist
        self.trackNumber = trackNumber
        self.discNumber = discNumber
        self.year = year
        self.genre = genre
        self.duration = duration
        self.fileSize = fileSize
        self.format = format
        self.artworkPath = artworkPath
        self.hasEmbeddedArtwork = hasEmbeddedArtwork
        self.addedAt = addedAt
        self.modifiedAt = modifiedAt
        self.rating = rating
        self.playCount = playCount
        self.lastPlayed = lastPlayed
        self.metadataSource = metadataSource
        self.fetchedFields = fetchedFields
        self.notes = notes
        self.isDemo = isDemo
    }

    public var displayTitle: String {
        let trimmed = title.trimmingCharacters(in: .whitespacesAndNewlines)
        return trimmed.isEmpty ? fileURL.deletingPathExtension().lastPathComponent : trimmed
    }

    public var displayArtist: String {
        let trimmed = artist.trimmingCharacters(in: .whitespacesAndNewlines)
        return trimmed.isEmpty || trimmed.lowercased() == "unknown artist" ? "Unknown artist" : trimmed
    }

    public var displayAlbum: String {
        let trimmed = album.trimmingCharacters(in: .whitespacesAndNewlines)
        return trimmed.isEmpty || trimmed.lowercased() == "unknown album" ? "Unknown album" : trimmed
    }

    public var albumKey: String {
        LibraryKey.album(artist: albumArtist.isEmpty ? artist : albumArtist, album: displayAlbum)
    }

    public var artistKey: String {
        LibraryKey.artist(displayArtist)
    }

    public var missingFields: [MetadataField] {
        var fields: [MetadataField] = []
        if title.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty { fields.append(.title) }
        if artist.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty { fields.append(.artist) }
        if album.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty { fields.append(.album) }
        if albumArtist.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty { fields.append(.albumArtist) }
        if (year ?? "").trimmingCharacters(in: .whitespacesAndNewlines).isEmpty { fields.append(.year) }
        if (genre ?? "").trimmingCharacters(in: .whitespacesAndNewlines).isEmpty { fields.append(.genre) }
        if artworkPath == nil && !hasEmbeddedArtwork { fields.append(.artwork) }
        return fields
    }

    public var isMetadataComplete: Bool { missingFields.isEmpty }

    public var durationText: String { Formatters.duration(duration) }
    public var fileSizeText: String { Formatters.bytes(fileSize) }
}

public struct AlbumAnnotation: Codable, Hashable, Sendable {
    public var description: String
    public var rating: Int
    public var year: String?
    public var genre: String?
    public var artworkPath: String?
    public var metadataSource: String?

    public init(
        description: String = "",
        rating: Int = 0,
        year: String? = nil,
        genre: String? = nil,
        artworkPath: String? = nil,
        metadataSource: String? = nil
    ) {
        self.description = description
        self.rating = rating
        self.year = year
        self.genre = genre
        self.artworkPath = artworkPath
        self.metadataSource = metadataSource
    }
}

public struct ArtistAnnotation: Codable, Hashable, Sendable {
    public var biography: String
    public var imageURL: String?
    public var country: String?
    public var yearsActive: String?
    public var metadataSource: String?

    public init(
        biography: String = "",
        imageURL: String? = nil,
        country: String? = nil,
        yearsActive: String? = nil,
        metadataSource: String? = nil
    ) {
        self.biography = biography
        self.imageURL = imageURL
        self.country = country
        self.yearsActive = yearsActive
        self.metadataSource = metadataSource
    }
}

public struct AlbumSummary: Identifiable, Hashable, Sendable {
    public let id: String
    public let title: String
    public let artist: String
    public let year: String?
    public let genre: String?
    public let description: String
    public let rating: Int
    public let artworkPath: String?
    public let trackCount: Int
    public let duration: TimeInterval
    public let isDemo: Bool

    public var durationText: String { Formatters.duration(duration) }
    public var subtitle: String {
        [artist, year].compactMap { value in
            guard let value, !value.isEmpty else { return nil }
            return value
        }.joined(separator: " · ")
    }
}

public struct ArtistSummary: Identifiable, Hashable, Sendable {
    public let id: String
    public let name: String
    public let biography: String
    public let country: String?
    public let albumCount: Int
    public let trackCount: Int
    public let artworkPath: String?
    public let isDemo: Bool
}

public struct LibrarySnapshot: Codable, Sendable {
    public var tracks: [Track]
    public var albumAnnotations: [String: AlbumAnnotation]
    public var artistAnnotations: [String: ArtistAnnotation]
    public var podcasts: [PodcastShow]
    public var audiobooks: [Audiobook]
    public var scrobbles: [ScrobbleEntry]
    public var savedAt: Date

    public init(
        tracks: [Track] = [],
        albumAnnotations: [String: AlbumAnnotation] = [:],
        artistAnnotations: [String: ArtistAnnotation] = [:],
        podcasts: [PodcastShow] = [],
        audiobooks: [Audiobook] = [],
        scrobbles: [ScrobbleEntry] = [],
        savedAt: Date = Date()
    ) {
        self.tracks = tracks
        self.albumAnnotations = albumAnnotations
        self.artistAnnotations = artistAnnotations
        self.podcasts = podcasts
        self.audiobooks = audiobooks
        self.scrobbles = scrobbles
        self.savedAt = savedAt
    }

    public static let empty = LibrarySnapshot()

    public var albums: [AlbumSummary] {
        let grouped = Dictionary(grouping: tracks, by: \.albumKey)
        return grouped.compactMap { key, tracks in
            guard let first = tracks.sorted(by: { ($0.discNumber ?? 0, $0.trackNumber ?? 0) < ($1.discNumber ?? 0, $1.trackNumber ?? 0) }).first else {
                return nil
            }
            let annotation = albumAnnotations[key] ?? AlbumAnnotation()
            let year = annotation.year ?? tracks.compactMap(\.year).first
            let genre = annotation.genre ?? tracks.compactMap(\.genre).first
            return AlbumSummary(
                id: key,
                title: annotation.description.isEmpty ? first.displayAlbum : first.displayAlbum,
                artist: first.albumArtist.isEmpty ? first.displayArtist : first.albumArtist,
                year: year,
                genre: genre,
                description: annotation.description,
                rating: annotation.rating,
                artworkPath: annotation.artworkPath ?? tracks.compactMap(\.artworkPath).first,
                trackCount: tracks.count,
                duration: tracks.reduce(0) { $0 + $1.duration },
                isDemo: tracks.allSatisfy(\.isDemo)
            )
        }
        .sorted { lhs, rhs in
            lhs.title.localizedCaseInsensitiveCompare(rhs.title) == .orderedAscending
        }
    }

    public var artists: [ArtistSummary] {
        let grouped = Dictionary(grouping: tracks, by: \.artistKey)
        return grouped.compactMap { key, tracks in
            guard let first = tracks.first else { return nil }
            let annotation = artistAnnotations[key] ?? ArtistAnnotation()
            let albums = Dictionary(grouping: tracks, by: \.albumKey).count
            return ArtistSummary(
                id: key,
                name: first.displayArtist,
                biography: annotation.biography,
                country: annotation.country,
                albumCount: albums,
                trackCount: tracks.count,
                artworkPath: tracks.compactMap(\.artworkPath).first,
                isDemo: tracks.allSatisfy(\.isDemo)
            )
        }
        .sorted { $0.name.localizedCaseInsensitiveCompare($1.name) == .orderedAscending }
    }

    public var tracksMissingMetadata: [Track] {
        tracks.filter { !$0.isMetadataComplete }
    }

    public var queuedScrobbles: [ScrobbleEntry] {
        scrobbles.filter { $0.status == .queued || $0.status == .failed }
    }
}

// MARK: - Device

public struct DeviceInfo: Identifiable, Codable, Hashable, Sendable {
    public let id: String
    public var name: String
    public var mountURL: URL
    public var filesystem: String
    public var isHFS: Bool
    public var isReadOnly: Bool
    public var isRemovable: Bool
    public var totalCapacity: Int64
    public var availableCapacity: Int64
    public var hasHelikonFolder: Bool
    public var hasIPodControl: Bool
    public var hasRockboxFolder: Bool
    public var model: String?
    public var busProtocol: String?

    public init(
        id: String,
        name: String,
        mountURL: URL,
        filesystem: String,
        isHFS: Bool,
        isReadOnly: Bool,
        isRemovable: Bool,
        totalCapacity: Int64,
        availableCapacity: Int64,
        hasHelikonFolder: Bool,
        hasIPodControl: Bool,
        hasRockboxFolder: Bool,
        model: String? = nil,
        busProtocol: String? = nil
    ) {
        self.id = id
        self.name = name
        self.mountURL = mountURL
        self.filesystem = filesystem
        self.isHFS = isHFS
        self.isReadOnly = isReadOnly
        self.isRemovable = isRemovable
        self.totalCapacity = totalCapacity
        self.availableCapacity = availableCapacity
        self.hasHelikonFolder = hasHelikonFolder
        self.hasIPodControl = hasIPodControl
        self.hasRockboxFolder = hasRockboxFolder
        self.model = model
        self.busProtocol = busProtocol
    }

    public var usedCapacity: Int64 { max(0, totalCapacity - availableCapacity) }
    public var usedFraction: Double {
        guard totalCapacity > 0 else { return 0 }
        return min(1, max(0, Double(usedCapacity) / Double(totalCapacity)))
    }

    public var kindLabel: String {
        if hasHelikonFolder { return "Helikon-OS" }
        if hasIPodControl { return "iPod" }
        if hasRockboxFolder { return "Rockbox volume" }
        return "HFS+ volume"
    }

    public var statusLabel: String {
        if !isHFS { return "Unsupported filesystem" }
        if isReadOnly { return "Read-only" }
        return "Mounted"
    }

    public var capacityText: String {
        "\(Formatters.bytes(availableCapacity)) free of \(Formatters.bytes(totalCapacity))"
    }
}

// MARK: - Podcasts and audiobooks

public enum EpisodeDownloadState: String, Codable, Hashable, Sendable {
    case remote
    case downloading
    case downloaded
    case failed
}

public struct PodcastEpisode: Identifiable, Codable, Hashable, Sendable {
    public var id: String
    public var title: String
    public var summary: String
    public var publishedAt: Date?
    public var duration: TimeInterval?
    public var enclosureURL: URL?
    public var localPath: String?
    public var downloadedAt: Date?
    public var downloadState: EpisodeDownloadState
    public var error: String?

    public init(
        id: String,
        title: String,
        summary: String = "",
        publishedAt: Date? = nil,
        duration: TimeInterval? = nil,
        enclosureURL: URL? = nil,
        localPath: String? = nil,
        downloadedAt: Date? = nil,
        downloadState: EpisodeDownloadState = .remote,
        error: String? = nil
    ) {
        self.id = id
        self.title = title
        self.summary = summary
        self.publishedAt = publishedAt
        self.duration = duration
        self.enclosureURL = enclosureURL
        self.localPath = localPath
        self.downloadedAt = downloadedAt
        self.downloadState = downloadState
        self.error = error
    }
}

public struct PodcastShow: Identifiable, Codable, Hashable, Sendable {
    public var id: UUID
    public var feedURL: URL
    public var title: String
    public var author: String
    public var summary: String
    public var artworkURLString: String?
    public var artworkPath: String?
    public var subscribedAt: Date
    public var lastRefreshed: Date?
    public var episodes: [PodcastEpisode]
    public var isDemo: Bool

    public init(
        id: UUID = UUID(),
        feedURL: URL,
        title: String,
        author: String = "",
        summary: String = "",
        artworkURLString: String? = nil,
        artworkPath: String? = nil,
        subscribedAt: Date = Date(),
        lastRefreshed: Date? = nil,
        episodes: [PodcastEpisode] = [],
        isDemo: Bool = false
    ) {
        self.id = id
        self.feedURL = feedURL
        self.title = title
        self.author = author
        self.summary = summary
        self.artworkURLString = artworkURLString
        self.artworkPath = artworkPath
        self.subscribedAt = subscribedAt
        self.lastRefreshed = lastRefreshed
        self.episodes = episodes
        self.isDemo = isDemo
    }

    public var downloadedCount: Int {
        episodes.filter { $0.downloadState == .downloaded }.count
    }
}

public struct Audiobook: Identifiable, Codable, Hashable, Sendable {
    public var id: String
    public var title: String
    public var author: String
    public var narrator: String?
    public var year: String?
    public var summary: String
    public var source: MetadataProvider
    public var sourceURLString: String?
    public var coverURLString: String?
    public var coverPath: String?
    public var localPaths: [String]
    public var remoteFileURLs: [String]
    public var duration: TimeInterval?
    public var isPublicDomain: Bool
    public var isDemo: Bool

    public init(
        id: String,
        title: String,
        author: String,
        narrator: String? = nil,
        year: String? = nil,
        summary: String = "",
        source: MetadataProvider,
        sourceURLString: String? = nil,
        coverURLString: String? = nil,
        coverPath: String? = nil,
        localPaths: [String] = [],
        remoteFileURLs: [String] = [],
        duration: TimeInterval? = nil,
        isPublicDomain: Bool = false,
        isDemo: Bool = false
    ) {
        self.id = id
        self.title = title
        self.author = author
        self.narrator = narrator
        self.year = year
        self.summary = summary
        self.source = source
        self.sourceURLString = sourceURLString
        self.coverURLString = coverURLString
        self.coverPath = coverPath
        self.localPaths = localPaths
        self.remoteFileURLs = remoteFileURLs
        self.duration = duration
        self.isPublicDomain = isPublicDomain
        self.isDemo = isDemo
    }

    public var fileCount: Int { localPaths.count }
    public var subtitle: String {
        [author, narrator.map { "Read by \($0)" }, year]
            .compactMap { $0 }
            .filter { !$0.isEmpty }
            .joined(separator: " · ")
    }
}

// MARK: - Scrobbling

public enum ScrobbleSource: String, Codable, Hashable, Sendable {
    case manual
    case importedLog
    case playHistory
}

public enum ScrobbleStatus: String, Codable, Hashable, Sendable {
    case queued
    case sending
    case sent
    case failed
}

public struct ScrobbleEntry: Identifiable, Codable, Hashable, Sendable {
    public var id: UUID
    public var artist: String
    public var title: String
    public var album: String
    public var albumArtist: String
    public var timestamp: Date
    public var duration: TimeInterval?
    public var source: ScrobbleSource
    public var status: ScrobbleStatus
    public var lastAttempt: Date?
    public var error: String?
    public var isDemo: Bool

    public init(
        id: UUID = UUID(),
        artist: String,
        title: String,
        album: String = "",
        albumArtist: String = "",
        timestamp: Date,
        duration: TimeInterval? = nil,
        source: ScrobbleSource = .manual,
        status: ScrobbleStatus = .queued,
        lastAttempt: Date? = nil,
        error: String? = nil,
        isDemo: Bool = false
    ) {
        self.id = id
        self.artist = artist
        self.title = title
        self.album = album
        self.albumArtist = albumArtist
        self.timestamp = timestamp
        self.duration = duration
        self.source = source
        self.status = status
        self.lastAttempt = lastAttempt
        self.error = error
        self.isDemo = isDemo
    }
}

// MARK: - Metadata

public struct MetadataCandidate: Identifiable, Codable, Hashable, Sendable {
    public var id: String
    public var source: MetadataProvider
    public var entity: MetadataEntity
    public var title: String
    public var subtitle: String
    public var detail: String?
    public var year: String?
    public var genre: String?
    public var artworkURLString: String?
    public var externalID: String?
    public var score: Double
    public var patch: [MetadataField: String]
    public var trackCount: Int?
    public var duration: TimeInterval?

    public init(
        id: String,
        source: MetadataProvider,
        entity: MetadataEntity,
        title: String,
        subtitle: String,
        detail: String? = nil,
        year: String? = nil,
        genre: String? = nil,
        artworkURLString: String? = nil,
        externalID: String? = nil,
        score: Double = 0,
        patch: [MetadataField: String] = [:],
        trackCount: Int? = nil,
        duration: TimeInterval? = nil
    ) {
        self.id = id
        self.source = source
        self.entity = entity
        self.title = title
        self.subtitle = subtitle
        self.detail = detail
        self.year = year
        self.genre = genre
        self.artworkURLString = artworkURLString
        self.externalID = externalID
        self.score = score
        self.patch = patch
        self.trackCount = trackCount
        self.duration = duration
    }
}

public enum MetadataEntity: String, Codable, Hashable, Sendable {
    case track
    case album
    case artist
    case podcast
    case audiobook
}

// MARK: - Transfer

public enum TransferConflictPolicy: String, Codable, CaseIterable, Hashable, Sendable {
    case skip
    case replaceWithBackup
    case keepBoth

    public var title: String {
        switch self {
        case .skip: return "Skip existing"
        case .replaceWithBackup: return "Replace after backup"
        case .keepBoth: return "Keep both"
        }
    }
}

public enum TransferItemStatus: String, Codable, Hashable, Sendable {
    case planned
    case copying
    case verifying
    case complete
    case skipped
    case failed
}

public struct TransferItem: Identifiable, Codable, Hashable, Sendable {
    public var id: UUID
    public var sourceURL: URL
    public var destinationURL: URL
    public var relativePath: String
    public var byteCount: Int64
    public var kind: MediaKind
    public var conflict: Bool
    public var status: TransferItemStatus
    public var progress: Double
    public var checksum: String?
    public var backupURL: URL?
    public var error: String?

    public init(
        id: UUID = UUID(),
        sourceURL: URL,
        destinationURL: URL,
        relativePath: String,
        byteCount: Int64,
        kind: MediaKind,
        conflict: Bool = false,
        status: TransferItemStatus = .planned,
        progress: Double = 0,
        checksum: String? = nil,
        backupURL: URL? = nil,
        error: String? = nil
    ) {
        self.id = id
        self.sourceURL = sourceURL
        self.destinationURL = destinationURL
        self.relativePath = relativePath
        self.byteCount = byteCount
        self.kind = kind
        self.conflict = conflict
        self.status = status
        self.progress = progress
        self.checksum = checksum
        self.backupURL = backupURL
        self.error = error
    }
}

public struct TransferPlan: Identifiable, Codable, Hashable, Sendable {
    public var id: UUID
    public var destinationRoot: URL
    public var deviceName: String?
    public var items: [TransferItem]
    public var conflictPolicy: TransferConflictPolicy
    public var createdAt: Date

    public init(
        id: UUID = UUID(),
        destinationRoot: URL,
        deviceName: String? = nil,
        items: [TransferItem] = [],
        conflictPolicy: TransferConflictPolicy = .replaceWithBackup,
        createdAt: Date = Date()
    ) {
        self.id = id
        self.destinationRoot = destinationRoot
        self.deviceName = deviceName
        self.items = items
        self.conflictPolicy = conflictPolicy
        self.createdAt = createdAt
    }

    public var totalBytes: Int64 { items.reduce(0) { $0 + $1.byteCount } }
    public var conflictCount: Int { items.filter(\.conflict).count }
    public var totalSizeText: String { Formatters.bytes(totalBytes) }
}

public struct TransferReport: Codable, Hashable, Sendable {
    public var completed: Int
    public var skipped: Int
    public var failed: Int
    public var bytesTransferred: Int64
    public var backupFolder: URL?
    public var errors: [String]

    public init(
        completed: Int = 0,
        skipped: Int = 0,
        failed: Int = 0,
        bytesTransferred: Int64 = 0,
        backupFolder: URL? = nil,
        errors: [String] = []
    ) {
        self.completed = completed
        self.skipped = skipped
        self.failed = failed
        self.bytesTransferred = bytesTransferred
        self.backupFolder = backupFolder
        self.errors = errors
    }
}

// MARK: - Settings

public struct AppSettings: Codable, Equatable, Sendable {
    public var networkEnabled: Bool
    public var metadataProviders: Set<MetadataProvider>
    public var writeMP3TagsOnTransfer: Bool
    public var verifyTransfers: Bool
    public var backupBeforeReplace: Bool
    public var defaultMusicFolder: String
    public var defaultPodcastFolder: String
    public var defaultAudiobookFolder: String
    public var conflictPolicy: TransferConflictPolicy
    public var scrobblingEnabled: Bool
    public var lastFMAPIKey: String
    public var lastFMUsername: String
    public var demoLibraryEnabled: Bool

    public init(
        networkEnabled: Bool = true,
        metadataProviders: Set<MetadataProvider> = [.musicBrainz, .iTunes],
        writeMP3TagsOnTransfer: Bool = true,
        verifyTransfers: Bool = true,
        backupBeforeReplace: Bool = true,
        defaultMusicFolder: String = "Music/Helikon Companion",
        defaultPodcastFolder: String = "Podcasts",
        defaultAudiobookFolder: String = "Audiobooks",
        conflictPolicy: TransferConflictPolicy = .replaceWithBackup,
        scrobblingEnabled: Bool = false,
        lastFMAPIKey: String = "",
        lastFMUsername: String = "",
        demoLibraryEnabled: Bool = false
    ) {
        self.networkEnabled = networkEnabled
        self.metadataProviders = metadataProviders
        self.writeMP3TagsOnTransfer = writeMP3TagsOnTransfer
        self.verifyTransfers = verifyTransfers
        self.backupBeforeReplace = backupBeforeReplace
        self.defaultMusicFolder = defaultMusicFolder
        self.defaultPodcastFolder = defaultPodcastFolder
        self.defaultAudiobookFolder = defaultAudiobookFolder
        self.conflictPolicy = conflictPolicy
        self.scrobblingEnabled = scrobblingEnabled
        self.lastFMAPIKey = lastFMAPIKey
        self.lastFMUsername = lastFMUsername
        self.demoLibraryEnabled = demoLibraryEnabled
    }

    public static let `default` = AppSettings()
}

// MARK: - Library grouping keys

public enum LibraryKey {
    public static func album(artist: String, album: String) -> String {
        "album:\(normalize(artist))|\(normalize(album))"
    }

    public static func artist(_ artist: String) -> String {
        "artist:\(normalize(artist))"
    }

    public static func normalize(_ value: String) -> String {
        value
            .folding(options: [.caseInsensitive, .diacriticInsensitive], locale: .current)
            .trimmingCharacters(in: .whitespacesAndNewlines)
            .replacingOccurrences(of: "\\s+", with: " ", options: .regularExpression)
    }
}
