import Foundation

public enum InstallationStepState: String, Codable, Hashable, Sendable {
    case ready
    case blocked
    case informational

    public var label: String {
        switch self {
        case .ready: return "Prepared"
        case .blocked: return "Waiting"
        case .informational: return "Reference"
        }
    }
}

public struct InstallationStep: Identifiable, Hashable, Sendable {
    public let id: Int
    public let title: String
    public let detail: String
    public let state: InstallationStepState

    public init(id: Int, title: String, detail: String, state: InstallationStepState) {
        self.id = id
        self.title = title
        self.detail = detail
        self.state = state
    }
}

public struct InstallReadiness: Hashable, Sendable {
    public let steps: [InstallationStep]
    public let canInstall: Bool
    public let summary: String
    public let limitations: [String]

    public init(steps: [InstallationStep], canInstall: Bool, summary: String, limitations: [String]) {
        self.steps = steps
        self.canInstall = canInstall
        self.summary = summary
        self.limitations = limitations
    }
}

public enum InstallationGuide {
    public static func readiness(device: DeviceInfo?, backupVerified: Bool) -> InstallReadiness {
        let volumeReady = device?.isHFS == true && device?.isReadOnly == false
        let helikonPresent = device?.hasHelikonFolder == true
        let steps: [InstallationStep] = [
            InstallationStep(
                id: 1,
                title: "Confirm the volume",
                detail: device == nil
                    ? "Connect the iPod in Disk Mode. The app only accepts a mounted, writable HFS+ volume."
                    : "\(device?.name ?? "Volume") is \(device?.filesystem ?? "unknown") and \(device?.statusLabel.lowercased() ?? "not mounted").",
                state: volumeReady ? .ready : .blocked
            ),
            InstallationStep(
                id: 2,
                title: "Make a verified backup",
                detail: backupVerified
                    ? "A backup manifest has been written and read back this session."
                    : "Back up the full library and confirm the restore path before any bootloader work.",
                state: backupVerified ? .ready : .blocked
            ),
            InstallationStep(
                id: 3,
                title: "Check HFS+ and the journal",
                detail: "The installer will refuse a FAT volume, a dirty journal or an unknown partition layout. This build does not run a repair.",
                state: volumeReady ? .informational : .blocked
            ),
            InstallationStep(
                id: 4,
                title: "Install the signed bootloader",
                detail: "Bootloader changes are DFU-only and never use --single. No release image exists yet, so this action is disabled.",
                state: .blocked
            ),
            InstallationStep(
                id: 5,
                title: "Install signed firmware",
                detail: helikonPresent
                    ? "This volume already has a .helikon folder. A future installer will stage and verify the signed image before boot."
                    : "A future installer will stage a signed image under .helikon/update/ and verify it before the first boot.",
                state: .blocked
            ),
            InstallationStep(
                id: 6,
                title: "Run the first-boot check",
                detail: "The first boot should report the firmware version, image hash, bootstate and a clean filesystem check before the library is opened.",
                state: .blocked
            )
        ]

        return InstallReadiness(
            steps: steps,
            canInstall: false,
            summary: "The installer is not part of this build. The guide and preflight checks are here so the release flow can be tested without inventing a signed image.",
            limitations: [
                "No production bootloader or signed firmware image ships with this app.",
                "The app does not format, repartition, restore or erase a device.",
                "DFU installation and Apple restore guidance will be added with the signed release.",
                "A generated backup still needs a recorded restore rehearsal before it is called proven."
            ]
        )
    }
}

public enum DemoLibrary {
    public static func snapshot() -> LibrarySnapshot {
        let now = Date()
        var tracks: [Track] = []
        var annotations: [String: AlbumAnnotation] = [:]
        var artistAnnotations: [String: ArtistAnnotation] = [:]

        func addAlbum(
            artist: String,
            album: String,
            year: String,
            genre: String,
            description: String,
            rating: Int,
            songs: [(String, TimeInterval)]
        ) {
            let key = LibraryKey.album(artist: artist, album: album)
            for (index, song) in songs.enumerated() {
                tracks.append(Track(
                    fileURL: URL(string: "demo://music/\(key.addingPercentEncoding(withAllowedCharacters: .alphanumerics) ?? "album")/\(index + 1).mp3")!,
                    title: song.0,
                    artist: artist,
                    album: album,
                    albumArtist: artist,
                    trackNumber: index + 1,
                    year: year,
                    genre: genre,
                    duration: song.1,
                    fileSize: 7_200_000,
                    format: "MP3",
                    hasEmbeddedArtwork: true,
                    addedAt: now.addingTimeInterval(-86400 * Double(20 - index)),
                    modifiedAt: now,
                    rating: 0,
                    isDemo: true
                ))
            }
            annotations[key] = AlbumAnnotation(
                description: description,
                rating: rating,
                year: year,
                genre: genre,
                metadataSource: "Sample library"
            )
            let artistKey = LibraryKey.artist(artist)
            if artistAnnotations[artistKey] == nil {
                artistAnnotations[artistKey] = ArtistAnnotation(
                    biography: "A sample artist in the companion preview. Replace this with a searched biography or your own notes.",
                    country: "Sample data",
                    metadataSource: "Sample library"
                )
            }
        }

        addAlbum(
            artist: "Aster Vale",
            album: "The Cartographer",
            year: "2024",
            genre: "Ambient",
            description: "A slow, map-like record: four pieces that trace a coastline rather than arrive at a chorus.",
            rating: 5,
            songs: [
                ("Ink and Latitude", 284),
                ("The Quiet Meridian", 331),
                ("Fold the Coast", 295),
                ("A Small, Accurate Sky", 402)
            ]
        )
        addAlbum(
            artist: "Marin House",
            album: "Low Tide",
            year: "2023",
            genre: "Folk",
            description: "Close-miked strings, room noise and songs that leave space around the words.",
            rating: 4,
            songs: [
                ("Salt Room", 218),
                ("Low Tide", 256),
                ("What the Water Kept", 302)
            ]
        )
        addAlbum(
            artist: "The Quiet Orchard",
            album: "Field Notes",
            year: "2022",
            genre: "Post-rock",
            description: "Four field recordings turned into patient, instrumental songs.",
            rating: 4,
            songs: [
                ("Gate Open", 267),
                ("Pale Grass", 311),
                ("Report from the Hill", 345),
                ("Return Path", 289)
            ]
        )
        addAlbum(
            artist: "Ivo Lane",
            album: "Northbound",
            year: "2025",
            genre: "Jazz",
            description: "A small-room quartet session with the windows open.",
            rating: 3,
            songs: [
                ("Platform Two", 201),
                ("Northbound", 377),
                ("Last Call at the Station", 264)
            ]
        )

        tracks.append(Track(
            fileURL: URL(string: "demo://music/Unsorted/03 untitled session.mp3")!,
            title: "Untitled session",
            artist: "Aster Vale",
            album: "The Cartographer",
            albumArtist: "Aster Vale",
            year: nil,
            genre: nil,
            duration: 187,
            fileSize: 4_500_000,
            format: "MP3",
            addedAt: now.addingTimeInterval(-3800),
            modifiedAt: now,
            isDemo: true
        ))

        let podcast = PodcastShow(
            feedURL: URL(string: "https://example.com/the-long-way-round.xml")!,
            title: "The Long Way Round",
            author: "Sample audio",
            summary: "A sample podcast feed used to preview subscriptions, episode states and transfer controls.",
            artworkURLString: nil,
            subscribedAt: now.addingTimeInterval(-86400 * 12),
            lastRefreshed: now.addingTimeInterval(-3600),
            episodes: [
                PodcastEpisode(
                    id: "demo-episode-1",
                    title: "A Road With No Name",
                    summary: "A conversation about taking the route without the fastest arrival time.",
                    publishedAt: now.addingTimeInterval(-86400 * 2),
                    duration: 2840,
                    enclosureURL: URL(string: "https://example.com/episode-1.mp3")
                ),
                PodcastEpisode(
                    id: "demo-episode-2",
                    title: "The Cartographer's Desk",
                    summary: "Maps, memory and the useful lie of a straight line.",
                    publishedAt: now.addingTimeInterval(-86400 * 9),
                    duration: 2515,
                    enclosureURL: URL(string: "https://example.com/episode-2.mp3")
                ),
                PodcastEpisode(
                    id: "demo-episode-3",
                    title: "Notes From the Coast",
                    summary: "What the tide leaves behind, and what it takes back.",
                    publishedAt: now.addingTimeInterval(-86400 * 16),
                    duration: 3022,
                    enclosureURL: URL(string: "https://example.com/episode-3.mp3")
                )
            ],
            isDemo: true
        )

        let audiobooks = [
            Audiobook(
                id: "demo-audiobook-1",
                title: "The Open Sea",
                author: "Ada Mercer",
                narrator: "Sample narrator",
                year: "1908",
                summary: "A sample public-domain-style reading, used to preview the audiobook shelf and transfer controls.",
                source: .librivox,
                duration: 18740,
                isPublicDomain: true,
                isDemo: true
            ),
            Audiobook(
                id: "demo-audiobook-2",
                title: "A History of Small Things",
                author: "E. M. Harrow",
                year: "1911",
                summary: "A sample audiobook record. The app treats this as a catalogue entry, not a downloaded file.",
                source: .openLibrary,
                isPublicDomain: false,
                isDemo: true
            )
        ]

        return LibrarySnapshot(
            tracks: tracks,
            albumAnnotations: annotations,
            artistAnnotations: artistAnnotations,
            podcasts: [podcast],
            audiobooks: audiobooks,
            scrobbles: [
                ScrobbleEntry(
                    artist: "Aster Vale",
                    title: "Ink and Latitude",
                    album: "The Cartographer",
                    albumArtist: "Aster Vale",
                    timestamp: now.addingTimeInterval(-1800),
                    duration: 284,
                    status: .queued,
                    isDemo: true
                ),
                ScrobbleEntry(
                    artist: "Marin House",
                    title: "Salt Room",
                    album: "Low Tide",
                    albumArtist: "Marin House",
                    timestamp: now.addingTimeInterval(-5400),
                    duration: 218,
                    status: .queued,
                    isDemo: true
                )
            ]
        )
    }
}
