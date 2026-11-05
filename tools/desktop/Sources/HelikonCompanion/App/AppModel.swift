import AppKit
import CompanionCore
import Foundation
import SwiftUI
import UniformTypeIdentifiers

struct AppAlert: Identifiable {
    let id = UUID()
    let title: String
    let message: String
}

struct ActivityEntry: Identifiable {
    let id = UUID()
    let title: String
    let detail: String
    let date: Date
    let symbol: String
    let tint: Color
}

struct MetadataSearchContext: Identifiable {
    let id = UUID()
    var entity: MetadataEntity
    var title: String
    var subtitle: String
    var candidates: [MetadataCandidate]
    var warnings: [String]
    var isLoading: Bool
    var targetTrackID: UUID?
    var targetAlbumID: String?
    var targetArtistID: String?
    var appliedMessage: String?
}

@MainActor
final class AppModel: ObservableObject {
    static let version = "0.1.0"

    @Published var section: CompanionSection = .overview
    @Published var isShowingSplash = true
    @Published var settings: AppSettings
    @Published var snapshot: LibrarySnapshot
    @Published var devices: [DeviceInfo] = []
    @Published var selectedDeviceID: String?
    @Published var isRefreshingDevices = false
    @Published var isScanning = false
    @Published var scanProgress = ""
    @Published var alert: AppAlert?
    @Published var statusMessage: String?
    @Published var activities: [ActivityEntry] = []
    @Published var metadataSearch: MetadataSearchContext?
    @Published var transferPlan: TransferPlan?
    @Published var transferReport: TransferReport?
    @Published var isTransferring = false
    @Published var transferProgressText = ""
    @Published var podcastSearchResults: [PodcastSearchResult] = []
    @Published var isSearchingPodcasts = false
    @Published var audiobookSearchResults: [Audiobook] = []
    @Published var isSearchingAudiobooks = false
    @Published var lastFMAuthURL: URL?
    @Published var lastFMStatus = ""
    @Published var backupVerified = false
    @Published var customDestinations: [MediaKind: URL] = [:]

    private let repository = LibraryRepository()
    private let scanner = LibraryScanner()
    private let metadataService = MetadataService()
    private let podcastService = PodcastService()
    private let audiobookService = AudiobookService()
    private let transferService = TransferService()
    private let backupService = BackupService()
    private let lastFMClient = LastFMClient()
    private var lastFMAuthToken: String?
    private var saveTask: Task<Void, Never>?

    init() {
        var loadedSettings = JSONStore.load(AppSettings.self, from: AppPaths.settingsURL, defaultValue: .default)
        if CommandLine.arguments.contains("--demo") {
            loadedSettings.demoLibraryEnabled = true
        }
        settings = loadedSettings
        snapshot = repository.load()
        if ProcessInfo.processInfo.arguments.contains("--demo"), snapshot.tracks.isEmpty {
            snapshot = DemoLibrary.snapshot()
            settings.demoLibraryEnabled = true
        }
        if snapshot.tracks.isEmpty && settings.demoLibraryEnabled {
            snapshot = DemoLibrary.snapshot()
        }
        if let index = CommandLine.arguments.firstIndex(of: "--section"),
           CommandLine.arguments.indices.contains(index + 1),
           let requested = CompanionSection(rawValue: CommandLine.arguments[index + 1]) {
            section = requested
        }
        if CommandLine.arguments.contains("--skip-splash") {
            isShowingSplash = false
        }
        try? AppPaths.prepare()
        record("Companion opened", detail: "Ready to inspect a mounted volume.", symbol: "sparkles", tint: HColor.blue)
        if !isShowingSplash {
            Task { await refreshDevices() }
        }
    }

    // MARK: - Computed state

    var selectedDevice: DeviceInfo? {
        guard let selectedDeviceID else { return devices.first }
        return devices.first(where: { $0.id == selectedDeviceID }) ?? devices.first
    }

    var trackCount: Int { snapshot.tracks.count }
    var albumCount: Int { snapshot.albums.count }
    var artistCount: Int { snapshot.artists.count }
    var missingMetadataCount: Int { snapshot.tracksMissingMetadata.count }
    var queuedScrobbleCount: Int { snapshot.queuedScrobbles.count }
    var downloadedPodcastCount: Int { snapshot.podcasts.reduce(0) { $0 + $1.downloadedCount } }
    var demoMode: Bool { snapshot.tracks.contains(where: \.isDemo) || snapshot.podcasts.contains(where: \.isDemo) }

    var installReadiness: InstallReadiness {
        InstallationGuide.readiness(device: selectedDevice, backupVerified: backupVerified)
    }

    var currentDestination: URL? {
        if let custom = customDestinations[.music] { return custom }
        if let device = selectedDevice {
            return device.mountURL.appendingPathComponent(settings.defaultMusicFolder, isDirectory: true)
        }
        return AppPaths.supportDirectory.appendingPathComponent("Staging/Music", isDirectory: true)
    }

    func destination(for kind: MediaKind) -> URL {
        if let custom = customDestinations[kind] { return custom }
        if let device = selectedDevice {
            switch kind {
            case .music: return device.mountURL.appendingPathComponent(settings.defaultMusicFolder, isDirectory: true)
            case .podcast: return device.mountURL.appendingPathComponent(settings.defaultPodcastFolder, isDirectory: true)
            case .audiobook: return device.mountURL.appendingPathComponent(settings.defaultAudiobookFolder, isDirectory: true)
            default: return device.mountURL.appendingPathComponent("Helikon Companion", isDirectory: true)
            }
        }
        let staging = AppPaths.supportDirectory.appendingPathComponent("Staging", isDirectory: true)
        switch kind {
        case .music: return staging.appendingPathComponent("Music", isDirectory: true)
        case .podcast: return staging.appendingPathComponent("Podcasts", isDirectory: true)
        case .audiobook: return staging.appendingPathComponent("Audiobooks", isDirectory: true)
        default: return staging
        }
    }

    func destinationLabel(for kind: MediaKind) -> String {
        if let device = selectedDevice {
            let path = destination(for: kind).path.replacingOccurrences(of: device.mountURL.path, with: "")
            return "\(device.name)\(path)"
        }
        return "Local staging · \(destination(for: kind).lastPathComponent)"
    }

    func isDemo(_ track: Track) -> Bool { track.isDemo }

    // MARK: - Devices

    func refreshDevices() async {
        isRefreshingDevices = true
        let found = await Task.detached(priority: .userInitiated) {
            DeviceDetector.detectDevices()
        }.value
        devices = found
        if let selectedDeviceID, !found.contains(where: { $0.id == selectedDeviceID }) {
            self.selectedDeviceID = found.first?.id
        } else if selectedDeviceID == nil {
            selectedDeviceID = found.first?.id
        }
        isRefreshingDevices = false
        if let device = selectedDevice {
            record("Volume found", detail: "\(device.name) · \(device.filesystem) · \(device.capacityText)", symbol: "externaldrive", tint: HColor.teal)
        }
    }

    func selectDevice(_ id: String) {
        selectedDeviceID = id
    }

    // MARK: - Welcome screen

    func dismissSplash() {
        isShowingSplash = false
        if devices.isEmpty {
            Task { await refreshDevices() }
        }
    }

    func showSplash() {
        isShowingSplash = true
    }

    // MARK: - Library

    func chooseAudioFiles() {
        let panel = NSOpenPanel()
        panel.title = "Add music to the companion library"
        panel.message = "Choose audio files or a folder. Files are read in place; nothing is copied until you review a transfer."
        panel.prompt = "Scan"
        panel.canChooseFiles = true
        panel.canChooseDirectories = true
        panel.allowsMultipleSelection = true
        panel.allowedContentTypes = [.audio, .folder]
        guard panel.runModal() == .OK else { return }
        let urls = panel.urls
        Task { await importLibrary(from: urls) }
    }

    func importLibrary(from urls: [URL]) async {
        guard !urls.isEmpty else { return }
        isScanning = true
        defer { isScanning = false }
        removeDemoContentIfNeeded()

        var allTracks: [Track] = []
        var errors: [String] = []
        var scannedDirectory = false
        for (index, url) in urls.enumerated() {
            scanProgress = "Reading \(url.lastPathComponent) (\(index + 1) of \(urls.count))"
            var isDirectory: ObjCBool = false
            if FileManager.default.fileExists(atPath: url.path, isDirectory: &isDirectory), isDirectory.boolValue {
                scannedDirectory = true
                let result = await scanner.scan(root: url) { [weak self] completed, total in
                    Task { @MainActor in
                        self?.scanProgress = "Reading \(url.lastPathComponent) · \(completed) of \(total)"
                    }
                }
                allTracks += result.tracks.map { track in
                    var track = track
                    track.relativePath = track.relativePath
                    return track
                }
                errors += result.errors
            } else {
                do {
                    allTracks.append(try await scanner.readTrack(at: url))
                } catch {
                    errors.append("\(url.lastPathComponent): \(error.localizedDescription)")
                }
            }
        }

        let scan = LibraryScanResult(tracks: allTracks, errors: errors, scannedCount: allTracks.count)
        let root = urls.count == 1 ? urls[0] : urls[0].deletingLastPathComponent()
        snapshot = LibraryMerger.merge(
            scan: scan,
            into: snapshot,
            root: root,
            prune: scannedDirectory && urls.count == 1 && !allTracks.isEmpty
        )
        scheduleSave()
        scanProgress = ""
        record("Library scanned", detail: "\(allTracks.count) tracks · \(errors.count) unreadable", symbol: "music.note.list", tint: HColor.rust)
        if !errors.isEmpty {
            alert = AppAlert(title: "Some files could not be read", message: errors.prefix(6).joined(separator: "\n"))
        }
    }

    func scanSelectedDevice() {
        guard let device = selectedDevice else {
            alert = AppAlert(title: "No volume connected", message: "Connect the iPod in Disk Mode or choose a folder to scan.")
            return
        }
        guard device.isHFS, !device.isReadOnly else {
            alert = AppAlert(title: "Volume cannot be scanned", message: "The companion app reads a mounted, writable HFS+ volume. This volume reports \(device.filesystem).")
            return
        }
        Task { await importLibrary(from: [device.mountURL]) }
    }

    func loadDemoLibrary() {
        if demoMode {
            alert = AppAlert(title: "Sample library already loaded", message: "Clear it first if you want a fresh sample set.")
            return
        }
        snapshot = DemoLibrary.snapshot()
        settings.demoLibraryEnabled = true
        saveSettings()
        scheduleSave()
        record("Sample library loaded", detail: "Preview only; sample tracks are not files and cannot be transferred.", symbol: "sparkles", tint: HColor.ochre)
    }

    func clearDemoLibrary() {
        snapshot.tracks.removeAll(where: \.isDemo)
        snapshot.podcasts.removeAll(where: \.isDemo)
        snapshot.audiobooks.removeAll(where: \.isDemo)
        snapshot.scrobbles.removeAll(where: \.isDemo)
        snapshot.albumAnnotations = snapshot.albumAnnotations.filter { key, _ in
            snapshot.tracks.contains(where: { $0.albumKey == key })
        }
        snapshot.artistAnnotations = snapshot.artistAnnotations.filter { key, _ in
            snapshot.tracks.contains(where: { $0.artistKey == key })
        }
        settings.demoLibraryEnabled = false
        saveSettings()
        scheduleSave()
        record("Sample library cleared", detail: "Only sample records were removed.", symbol: "trash", tint: HColor.muted)
    }

    func removeTrack(_ track: Track) {
        guard !track.isDemo else {
            alert = AppAlert(title: "Sample record", message: "Sample records are managed by Clear sample library.")
            return
        }
        snapshot = LibraryMerger.removeTrack(track.id, from: snapshot)
        scheduleSave()
    }

    func setRating(_ rating: Int, for track: Track) {
        snapshot = LibraryMerger.setRating(rating, for: track.id, in: snapshot)
        scheduleSave()
    }

    func setAlbumRating(_ rating: Int, albumID: String) {
        snapshot = LibraryMerger.setAlbumRating(rating, albumID: albumID, in: snapshot)
        scheduleSave()
    }

    func setAlbumDescription(_ description: String, albumID: String) {
        snapshot = LibraryMerger.setAlbumDescription(description, albumID: albumID, in: snapshot)
        scheduleSave()
    }

    func setArtistBiography(_ biography: String, artistID: String) {
        snapshot = LibraryMerger.setArtistBiography(biography, artistID: artistID, in: snapshot)
        scheduleSave()
    }

    func updateTrack(
        _ trackID: UUID,
        title: String,
        artist: String,
        album: String,
        albumArtist: String,
        year: String,
        genre: String,
        notes: String
    ) {
        guard let index = snapshot.tracks.firstIndex(where: { $0.id == trackID }) else { return }
        snapshot.tracks[index].title = title
        snapshot.tracks[index].artist = artist
        snapshot.tracks[index].album = album
        snapshot.tracks[index].albumArtist = albumArtist
        snapshot.tracks[index].year = year.isEmpty ? nil : year
        snapshot.tracks[index].genre = genre.isEmpty ? nil : genre
        snapshot.tracks[index].notes = notes.isEmpty ? nil : notes
        snapshot.tracks[index].fetchedFields.formUnion([.title, .artist, .album, .albumArtist, .year, .genre])
        snapshot.tracks[index].metadataSource = "Manual edit"
        scheduleSave()
        record("Metadata edited", detail: "\(artist) · \(title)", symbol: "pencil", tint: HColor.moss)
    }

    func openSupportFolder() {
        NSWorkspace.shared.open(AppPaths.supportDirectory)
    }

    func flash(_ message: String) {
        statusMessage = message
        Task {
            try? await Task.sleep(nanoseconds: 3_200_000_000)
            if statusMessage == message { statusMessage = nil }
        }
    }

    // MARK: - Metadata search

    func searchMetadata(for track: Track) {
        guard ensureNetwork() else { return }
        metadataSearch = MetadataSearchContext(
            entity: .track,
            title: track.displayTitle,
            subtitle: [track.displayArtist, track.displayAlbum].filter { $0 != "Unknown artist" && $0 != "Unknown album" }.joined(separator: " · "),
            candidates: [],
            warnings: [],
            isLoading: true,
            targetTrackID: track.id
        )
        Task { await runMetadataSearch(contextID: metadataSearch?.id) }
    }

    func searchMetadata(for album: AlbumSummary) {
        guard ensureNetwork() else { return }
        metadataSearch = MetadataSearchContext(
            entity: .album,
            title: album.title,
            subtitle: album.artist,
            candidates: [],
            warnings: [],
            isLoading: true,
            targetAlbumID: album.id
        )
        Task { await runMetadataSearch(contextID: metadataSearch?.id) }
    }

    func searchMetadata(for artist: ArtistSummary) {
        guard ensureNetwork() else { return }
        metadataSearch = MetadataSearchContext(
            entity: .artist,
            title: artist.name,
            subtitle: "\(artist.albumCount) albums · \(artist.trackCount) tracks",
            candidates: [],
            warnings: [],
            isLoading: true,
            targetArtistID: artist.id
        )
        Task { await runMetadataSearch(contextID: metadataSearch?.id) }
    }

    func searchMetadata(title: String, artist: String, album: String, entity: MetadataEntity) {
        guard ensureNetwork() else { return }
        metadataSearch = MetadataSearchContext(
            entity: entity,
            title: title.isEmpty ? "Untitled search" : title,
            subtitle: [artist, album].filter { !$0.isEmpty }.joined(separator: " · "),
            candidates: [],
            warnings: [],
            isLoading: true
        )
        Task { await runMetadataSearch(contextID: metadataSearch?.id) }
    }

    private func runMetadataSearch(contextID: UUID?) async {
        guard let contextID, var context = metadataSearch, context.id == contextID else { return }
        let response: MetadataSearchResponse
        switch context.entity {
        case .track:
            let track = snapshot.tracks.first(where: { $0.id == context.targetTrackID })
            response = await metadataService.searchTracks(
                title: context.title,
                artist: track?.displayArtist == "Unknown artist" ? "" : (track?.artist ?? context.subtitle),
                album: track?.album ?? "",
                providers: settings.metadataProviders
            )
        case .album:
            let album = snapshot.albums.first(where: { $0.id == context.targetAlbumID })
            response = await metadataService.searchAlbums(
                title: album?.title ?? context.title,
                artist: album?.artist ?? "",
                providers: settings.metadataProviders
            )
        case .artist:
            let artist = snapshot.artists.first(where: { $0.id == context.targetArtistID })
            response = await metadataService.searchArtists(
                name: artist?.name ?? context.title,
                providers: settings.metadataProviders
            )
        default:
            response = MetadataSearchResponse(warnings: ["This metadata entity is not searchable here."])
        }
        context.candidates = response.candidates
        context.warnings = response.warnings
        context.isLoading = false
        if metadataSearch?.id == contextID {
            metadataSearch = context
        }
    }

    func applyMetadata(_ candidate: MetadataCandidate) async {
        guard var context = metadataSearch else { return }
        let resolved = await resolveArtwork(candidate)
        if resolved.patch[.artwork] == nil, candidate.patch[.artwork] != nil {
            context.warnings.append("Artwork could not be downloaded; the text fields were kept.")
        }

        if let trackID = context.targetTrackID,
           let index = snapshot.tracks.firstIndex(where: { $0.id == trackID }) {
            snapshot.tracks[index] = LibraryMerger.apply(resolved, to: snapshot.tracks[index])
            record("Metadata applied", detail: "\(snapshot.tracks[index].displayArtist) · \(snapshot.tracks[index].displayTitle)", symbol: "tag", tint: HColor.moss)
        } else if let albumID = context.targetAlbumID {
            snapshot = LibraryMerger.apply(resolved, albumID: albumID, to: snapshot)
            record("Album metadata applied", detail: resolved.title, symbol: "square.stack", tint: HColor.moss)
        } else if let artistID = context.targetArtistID {
            snapshot = LibraryMerger.apply(resolved, artistID: artistID, to: snapshot)
            record("Artist metadata applied", detail: resolved.title, symbol: "person", tint: HColor.moss)
        }
        context.appliedMessage = "Applied from \(resolved.source.title)."
        metadataSearch = context
        scheduleSave()
    }

    func dismissMetadataSearch() {
        metadataSearch = nil
    }

    private func resolveArtwork(_ candidate: MetadataCandidate) async -> MetadataCandidate {
        guard var remote = candidate.patch[.artwork], remote.hasPrefix("http") else { return candidate }
        if let local = try? await ArtworkStore.cache(remoteURLString: remote) {
            remote = local
        } else {
            remote = ""
        }
        var copy = candidate
        if remote.isEmpty {
            copy.patch.removeValue(forKey: .artwork)
        } else {
            copy.patch[.artwork] = remote
        }
        return copy
    }

    private func ensureNetwork() -> Bool {
        guard settings.networkEnabled else {
            alert = AppAlert(
                title: "Network access is off",
                message: "Turn on metadata lookups in Settings. The app only makes a request when you press Search."
            )
            return false
        }
        return true
    }

    // MARK: - Scrobbling

    var lastFMSecretIsStored: Bool { KeychainStore.get(account: KeychainStore.lastFMSecretAccount) != nil }
    var lastFMSessionIsStored: Bool { KeychainStore.get(account: KeychainStore.lastFMSessionAccount) != nil }

    func saveLastFMSecret(_ secret: String) {
        let trimmed = secret.trimmingCharacters(in: .whitespacesAndNewlines)
        KeychainStore.set(trimmed.isEmpty ? nil : trimmed, account: KeychainStore.lastFMSecretAccount)
        lastFMStatus = trimmed.isEmpty ? "Secret removed." : "Secret saved in Keychain."
    }

    func beginLastFMAuthentication() async {
        guard let secret = KeychainStore.get(account: KeychainStore.lastFMSecretAccount), !secret.isEmpty else {
            alert = AppAlert(title: "Last.fm secret missing", message: "Paste your Last.fm API secret in Settings. It is stored in the macOS Keychain.")
            return
        }
        guard !settings.lastFMAPIKey.isEmpty else {
            alert = AppAlert(title: "Last.fm API key missing", message: "Add the API key from your Last.fm API account in Settings.")
            return
        }
        lastFMStatus = "Requesting a token…"
        do {
            let token = try await lastFMClient.authenticationToken(apiKey: settings.lastFMAPIKey, secret: secret)
            lastFMAuthToken = token
            lastFMAuthURL = await lastFMClient.authenticationURL(apiKey: settings.lastFMAPIKey, token: token)
            lastFMStatus = "Approve the app on Last.fm, then press Complete connection."
        } catch {
            lastFMStatus = error.localizedDescription
        }
    }

    func completeLastFMAuthentication() async {
        guard let secret = KeychainStore.get(account: KeychainStore.lastFMSecretAccount), !secret.isEmpty else {
            alert = AppAlert(title: "Last.fm secret missing", message: "The secret is not in the Keychain.")
            return
        }
        guard let token = lastFMAuthToken else {
            alert = AppAlert(title: "Start the connection first", message: "Request a token, approve it on Last.fm, then complete the connection.")
            return
        }
        do {
            let session = try await lastFMClient.session(apiKey: settings.lastFMAPIKey, secret: secret, token: token)
            KeychainStore.set(session.key, account: KeychainStore.lastFMSessionAccount)
            settings.lastFMUsername = session.name
            settings.scrobblingEnabled = true
            saveSettings()
            lastFMAuthURL = nil
            lastFMAuthToken = nil
            lastFMStatus = "Connected as \(session.name)."
            record("Last.fm connected", detail: session.name, symbol: "waveform.path.ecg", tint: HColor.plum)
        } catch {
            lastFMStatus = error.localizedDescription
        }
    }

    func disconnectLastFM() {
        KeychainStore.delete(account: KeychainStore.lastFMSessionAccount)
        settings.lastFMUsername = ""
        settings.scrobblingEnabled = false
        saveSettings()
        lastFMStatus = "Disconnected."
    }

    func addScrobble(for track: Track) {
        guard !track.isDemo else {
            alert = AppAlert(title: "Sample track", message: "Sample records have no audio file and cannot produce a real scrobble.")
            return
        }
        snapshot.scrobbles.append(ScrobbleEntry(
            artist: track.displayArtist,
            title: track.displayTitle,
            album: track.album,
            albumArtist: track.albumArtist,
            timestamp: Date(),
            duration: track.duration,
            source: .manual
        ))
        scheduleSave()
        record("Scrobble queued", detail: "\(track.displayArtist) · \(track.displayTitle)", symbol: "waveform.path.ecg", tint: HColor.plum)
    }

    func flushScrobbles() async {
        guard settings.scrobblingEnabled else {
            alert = AppAlert(title: "Scrobbling is off", message: "Enable scrobbling in Settings and connect Last.fm before sending the queue.")
            return
        }
        guard let secret = KeychainStore.get(account: KeychainStore.lastFMSecretAccount),
              let sessionKey = KeychainStore.get(account: KeychainStore.lastFMSessionAccount),
              !secret.isEmpty, !sessionKey.isEmpty else {
            alert = AppAlert(title: "Last.fm is not connected", message: "Add the API key and secret, then complete the Last.fm connection.")
            return
        }
        let queued = snapshot.scrobbles.filter { $0.status == .queued || $0.status == .failed }
        guard !queued.isEmpty else {
            lastFMStatus = "The scrobble queue is empty."
            return
        }

        for index in snapshot.scrobbles.indices where queued.contains(where: { $0.id == snapshot.scrobbles[index].id }) {
            snapshot.scrobbles[index].status = .sending
            snapshot.scrobbles[index].lastAttempt = Date()
        }
        scheduleSave()

        do {
            _ = try await lastFMClient.scrobble(
                queued,
                apiKey: settings.lastFMAPIKey,
                secret: secret,
                sessionKey: sessionKey
            )
            for index in snapshot.scrobbles.indices where queued.contains(where: { $0.id == snapshot.scrobbles[index].id }) {
                snapshot.scrobbles[index].status = .sent
                snapshot.scrobbles[index].error = nil
            }
            lastFMStatus = "Sent \(queued.count) scrobble\(queued.count == 1 ? "" : "s")."
            record("Scrobbles sent", detail: "\(queued.count) queued entries", symbol: "paperplane", tint: HColor.plum)
        } catch {
            for index in snapshot.scrobbles.indices where queued.contains(where: { $0.id == snapshot.scrobbles[index].id }) {
                snapshot.scrobbles[index].status = .failed
                snapshot.scrobbles[index].error = error.localizedDescription
            }
            lastFMStatus = error.localizedDescription
        }
        scheduleSave()
    }

    func importScrobbles() {
        let panel = NSOpenPanel()
        panel.title = "Import play history"
        panel.message = "Choose a Rockbox .scrobbler.log, a JSON play history, or a Helikon play log."
        panel.canChooseFiles = true
        panel.canChooseDirectories = false
        guard panel.runModal() == .OK, let url = panel.url else { return }
        importScrobbles(from: url)
    }

    func importDevicePlayHistory() {
        guard let device = selectedDevice else {
            alert = AppAlert(title: "No volume connected", message: "Connect the iPod to look for a play history file.")
            return
        }
        let candidates = [
            device.mountURL.appendingPathComponent(".scrobbler.log"),
            device.mountURL.appendingPathComponent(".helikon/playhistory.json"),
            device.mountURL.appendingPathComponent(".helikon/plays.jsonl")
        ]
        if let found = candidates.first(where: { FileManager.default.fileExists(atPath: $0.path) }) {
            importScrobbles(from: found)
        } else {
            alert = AppAlert(
                title: "No play history found",
                message: "The app looked for .scrobbler.log and .helikon/playhistory.json. No file was read and nothing was written to the volume."
            )
        }
    }

    private func importScrobbles(from url: URL) {
        do {
            let data = try Data(contentsOf: url)
            let imported = url.pathExtension.lowercased() == "log"
                ? ScrobbleImporter.parseRockboxLog(data)
                : ScrobbleImporter.parseJSON(data)
            guard !imported.isEmpty else {
                alert = AppAlert(title: "No playable history found", message: "The file was read but no artist, track and timestamp rows could be recovered.")
                return
            }
            let existing = Set(snapshot.scrobbles.map { "\($0.artist)|\($0.title)|\(Int($0.timestamp.timeIntervalSince1970))" })
            let fresh = imported.filter { !existing.contains("\($0.artist)|\($0.title)|\(Int($0.timestamp.timeIntervalSince1970))") }
            snapshot.scrobbles.append(contentsOf: fresh)
            scheduleSave()
            record("Play history imported", detail: "\(fresh.count) new rows from \(url.lastPathComponent)", symbol: "clock.arrow.circlepath", tint: HColor.plum)
            if fresh.count < imported.count {
                lastFMStatus = "Added \(fresh.count) rows; \(imported.count - fresh.count) were already in the queue."
            }
        } catch {
            alert = AppAlert(title: "Could not read the history", message: error.localizedDescription)
        }
    }

    // MARK: - Podcasts

    func searchPodcasts(term: String) async {
        guard ensureNetwork() else { return }
        isSearchingPodcasts = true
        defer { isSearchingPodcasts = false }
        do {
            podcastSearchResults = try await podcastService.searchDirectory(term: term)
        } catch {
            alert = AppAlert(title: "Podcast search failed", message: error.localizedDescription)
        }
    }

    func subscribePodcast(_ result: PodcastSearchResult) async {
        guard ensureNetwork() else { return }
        do {
            var show = try await podcastService.fetchFeed(url: result.feedURL)
            show.artworkURLString = result.artworkURLString ?? show.artworkURLString
            if let art = show.artworkURLString {
                show.artworkPath = try? await ArtworkStore.cache(remoteURLString: art)
            }
            snapshot.podcasts.removeAll(where: { $0.feedURL == show.feedURL })
            snapshot.podcasts.append(show)
            scheduleSave()
            record("Podcast subscribed", detail: show.title, symbol: "dot.radiowaves.left.and.right", tint: HColor.blue)
        } catch {
            alert = AppAlert(title: "Could not subscribe", message: error.localizedDescription)
        }
    }

    func addPodcastFeed(_ value: String) {
        guard let url = URL(string: value), url.scheme?.hasPrefix("http") == true else {
            alert = AppAlert(title: "Feed URL is not valid", message: "Paste the full http or https feed address.")
            return
        }
        guard ensureNetwork() else { return }
        Task {
            do {
                var show = try await podcastService.fetchFeed(url: url)
                if let art = show.artworkURLString {
                    show.artworkPath = try? await ArtworkStore.cache(remoteURLString: art)
                }
                snapshot.podcasts.removeAll(where: { $0.feedURL == url })
                snapshot.podcasts.append(show)
                scheduleSave()
                record("Podcast subscribed", detail: show.title, symbol: "dot.radiowaves.left.and.right", tint: HColor.blue)
            } catch {
                alert = AppAlert(title: "Could not read the feed", message: error.localizedDescription)
            }
        }
    }

    func refreshPodcast(_ show: PodcastShow) async {
        guard ensureNetwork() else { return }
        do {
            let refreshed = try await podcastService.refresh(show)
            if let index = snapshot.podcasts.firstIndex(where: { $0.id == show.id }) {
                snapshot.podcasts[index] = refreshed
                scheduleSave()
                record("Podcast refreshed", detail: refreshed.title, symbol: "arrow.clockwise", tint: HColor.blue)
            }
        } catch {
            alert = AppAlert(title: "Could not refresh the feed", message: error.localizedDescription)
        }
    }

    func removePodcast(_ show: PodcastShow) {
        snapshot.podcasts.removeAll(where: { $0.id == show.id })
        scheduleSave()
    }

    func downloadEpisode(showID: UUID, episodeID: String) async {
        guard let showIndex = snapshot.podcasts.firstIndex(where: { $0.id == showID }),
              let episodeIndex = snapshot.podcasts[showIndex].episodes.firstIndex(where: { $0.id == episodeID }),
              let remote = snapshot.podcasts[showIndex].episodes[episodeIndex].enclosureURL else { return }
        guard ensureNetwork() else { return }
        snapshot.podcasts[showIndex].episodes[episodeIndex].downloadState = .downloading
        do {
            let url = try await podcastService.downloadEpisode(snapshot.podcasts[showIndex].episodes[episodeIndex], show: snapshot.podcasts[showIndex])
            snapshot.podcasts[showIndex].episodes[episodeIndex].localPath = url.path
            snapshot.podcasts[showIndex].episodes[episodeIndex].downloadedAt = Date()
            snapshot.podcasts[showIndex].episodes[episodeIndex].downloadState = .downloaded
            snapshot.podcasts[showIndex].episodes[episodeIndex].error = nil
            scheduleSave()
            record("Episode downloaded", detail: snapshot.podcasts[showIndex].episodes[episodeIndex].title, symbol: "arrow.down.circle", tint: HColor.blue)
        } catch {
            snapshot.podcasts[showIndex].episodes[episodeIndex].downloadState = .failed
            snapshot.podcasts[showIndex].episodes[episodeIndex].error = error.localizedDescription
            alert = AppAlert(title: "Episode download failed", message: "\(remote.lastPathComponent): \(error.localizedDescription)")
        }
    }

    func transferEpisode(showID: UUID, episodeID: String) async {
        guard let show = snapshot.podcasts.first(where: { $0.id == showID }),
              let episode = show.episodes.first(where: { $0.id == episodeID }) else { return }
        if episode.downloadState != .downloaded || episode.localPath == nil {
            await downloadEpisode(showID: showID, episodeID: episodeID)
        }
        guard let path = snapshot.podcasts.first(where: { $0.id == showID })?
            .episodes.first(where: { $0.id == episodeID })?
            .localPath else { return }
        await prepareTransfer(files: [URL(fileURLWithPath: path)], kind: .podcast, relativeRoot: nil)
    }

    // MARK: - Audiobooks

    func searchAudiobooks(term: String) async {
        guard ensureNetwork() else { return }
        isSearchingAudiobooks = true
        defer { isSearchingAudiobooks = false }
        async let openLibrary = audiobookService.searchOpenLibrary(term: term)
        async let librivox = audiobookService.searchLibriVox(term: term)
        var combined: [Audiobook] = []
        var messages: [String] = []
        do { combined += try await openLibrary } catch { messages.append("Open Library: \(error.localizedDescription)") }
        do { combined += try await librivox } catch { messages.append("LibriVox: \(error.localizedDescription)") }
        audiobookSearchResults = combined.sorted { lhs, rhs in
            if lhs.isPublicDomain != rhs.isPublicDomain { return lhs.isPublicDomain }
            return lhs.title.localizedCaseInsensitiveCompare(rhs.title) == .orderedAscending
        }
        if combined.isEmpty, !messages.isEmpty {
            alert = AppAlert(title: "Audiobook search returned nothing", message: messages.joined(separator: "\n"))
        }
    }

    func addAudiobook(_ audiobook: Audiobook) {
        if !snapshot.audiobooks.contains(where: { $0.id == audiobook.id }) {
            snapshot.audiobooks.append(audiobook)
            scheduleSave()
            record("Audiobook saved", detail: audiobook.title, symbol: "book.closed", tint: HColor.ochre)
        }
    }

    func downloadAudiobook(_ audiobook: Audiobook) async {
        guard ensureNetwork() else { return }
        do {
            var updated = try await audiobookService.download(audiobook)
            updated.coverPath = try? await audiobookService.cacheCover(for: updated)
            if let index = snapshot.audiobooks.firstIndex(where: { $0.id == audiobook.id }) {
                snapshot.audiobooks[index] = updated
            } else {
                snapshot.audiobooks.append(updated)
            }
            scheduleSave()
            record("Audiobook downloaded", detail: "\(updated.title) · \(updated.localPaths.count) files", symbol: "arrow.down.circle", tint: HColor.ochre)
        } catch {
            alert = AppAlert(title: "Audiobook download failed", message: error.localizedDescription)
        }
    }

    func transferAudiobook(_ audiobook: Audiobook) async {
        if audiobook.localPaths.isEmpty, !audiobook.remoteFileURLs.isEmpty {
            await downloadAudiobook(audiobook)
        }
        guard let current = snapshot.audiobooks.first(where: { $0.id == audiobook.id }),
              !current.localPaths.isEmpty else { return }
        let files = current.localPaths.map { URL(fileURLWithPath: $0) }
        await prepareTransfer(
            files: files,
            kind: .audiobook,
            relativeRoot: files.first?.deletingLastPathComponent()
        )
    }

    // MARK: - Transfer

    func chooseTransferDestination(for kind: MediaKind) {
        let panel = NSOpenPanel()
        panel.title = "Choose a destination folder"
        panel.message = "The app writes only inside this folder and verifies each copy."
        panel.prompt = "Use Folder"
        panel.canChooseFiles = false
        panel.canChooseDirectories = true
        panel.canCreateDirectories = true
        panel.allowsMultipleSelection = false
        if let device = selectedDevice {
            panel.directoryURL = device.mountURL
        }
        guard panel.runModal() == .OK, let url = panel.url else { return }
        customDestinations[kind] = url
    }

    func prepareMusicTransfer() {
        let panel = NSOpenPanel()
        panel.title = "Choose music to transfer"
        panel.message = "Choose files or a folder. The app will plan the copy first; nothing is written until you confirm."
        panel.prompt = "Plan transfer"
        panel.canChooseFiles = true
        panel.canChooseDirectories = true
        panel.allowsMultipleSelection = true
        panel.allowedContentTypes = [.audio, .folder]
        guard panel.runModal() == .OK else { return }
        let urls = panel.urls
        Task { await prepareMusicTransfer(urls: urls) }
    }

    func prepareMusicTransfer(urls: [URL]) async {
        var files: [URL] = []
        var relativeRoot: URL?
        for url in urls {
            var isDirectory: ObjCBool = false
            if FileManager.default.fileExists(atPath: url.path, isDirectory: &isDirectory), isDirectory.boolValue {
                files += scanner.audioFiles(in: url)
                if relativeRoot == nil { relativeRoot = url }
            } else if AudioFileTypes.isAudio(url) {
                files.append(url)
            }
        }
        guard !files.isEmpty else {
            alert = AppAlert(title: "No audio files found", message: "Choose supported audio files or a folder that contains them.")
            return
        }
        await prepareTransfer(files: files, kind: .music, relativeRoot: relativeRoot)
    }

    func prepareTransfer(files: [URL], kind: MediaKind, relativeRoot: URL?) async {
        guard !files.isEmpty else { return }
        let destination = destination(for: kind)
        do {
            var plan = try await transferService.plan(
                files: files,
                destinationRoot: destination,
                relativeRoot: relativeRoot,
                kind: kind,
                conflictPolicy: settings.conflictPolicy
            )
            plan.deviceName = selectedDevice?.name
            transferPlan = plan
            transferReport = nil
            section = .transfer
            record("Transfer planned", detail: "\(plan.items.count) files · \(plan.totalSizeText) · \(plan.conflictCount) conflicts", symbol: "arrow.up.arrow.down", tint: HColor.teal)
            if kind == .music {
                Task { await indexTransferFiles(files, relativeRoot: relativeRoot) }
            }
        } catch {
            alert = AppAlert(title: "Could not plan the transfer", message: error.localizedDescription)
        }
    }

    /// Reads tags from the files in a plan so the metadata queue can help before
    /// the copy starts. It is read-only and never replaces a file.
    func indexTransferFiles(_ files: [URL], relativeRoot: URL?) async {
        guard !files.isEmpty else { return }
        var tracks: [Track] = []
        var errors = 0
        for file in files {
            do {
                tracks.append(try await scanner.readTrack(at: file, relativeTo: relativeRoot))
            } catch {
                errors += 1
            }
        }
        guard !tracks.isEmpty else { return }
        removeDemoContentIfNeeded()
        let scan = LibraryScanResult(tracks: tracks, errors: [], scannedCount: tracks.count)
        let root = relativeRoot ?? files[0].deletingLastPathComponent()
        snapshot = LibraryMerger.merge(scan: scan, into: snapshot, root: root, prune: false)
        scheduleSave()
        if errors > 0 {
            flash("Indexed \(tracks.count) transfer files; \(errors) could not be read.")
        } else {
            flash("Indexed \(tracks.count) transfer files for metadata.")
        }
    }

    func missingMetadataCount(inPlan plan: TransferPlan) -> Int {
        plan.items.reduce(0) { count, item in
            guard let track = snapshot.tracks.first(where: { $0.fileURL.standardizedFileURL == item.sourceURL.standardizedFileURL }) else {
                return count
            }
            return count + (track.isMetadataComplete ? 0 : 1)
        }
    }

    func executeTransfer() async {
        guard let plan = transferPlan, !isTransferring else { return }
        isTransferring = true
        transferProgressText = "Preparing…"
        transferReport = nil
        var tagsBySource: [URL: ID3TagWriter.Tags] = [:]
        if settings.writeMP3TagsOnTransfer {
            for item in plan.items where item.kind == .music && item.sourceURL.pathExtension.lowercased() == "mp3" {
                if let track = snapshot.tracks.first(where: { $0.fileURL.standardizedFileURL == item.sourceURL.standardizedFileURL }) {
                    tagsBySource[item.sourceURL] = ID3TagWriter.Tags(track: track)
                }
            }
        }
        do {
            let report = try await transferService.execute(
                plan: plan,
                verify: settings.verifyTransfers,
                tagsBySource: tagsBySource
            ) { [weak self] item in
                Task { @MainActor in
                    self?.updateTransferItem(item)
                }
            }
            transferReport = report
            isTransferring = false
            transferProgressText = ""
            if report.failed == 0 {
                record("Transfer complete", detail: "\(report.completed) files · \(Formatters.bytes(report.bytesTransferred))", symbol: "checkmark.circle", tint: HColor.moss)
            } else {
                alert = AppAlert(title: "Transfer finished with errors", message: report.errors.prefix(8).joined(separator: "\n"))
            }
            if !snapshot.tracks.isEmpty {
                for index in snapshot.tracks.indices {
                    if plan.items.contains(where: { $0.sourceURL == snapshot.tracks[index].fileURL && $0.status == .complete }) {
                        snapshot.tracks[index].modifiedAt = Date()
                    }
                }
                scheduleSave()
            }
        } catch {
            isTransferring = false
            transferProgressText = ""
            alert = AppAlert(title: "Transfer stopped", message: error.localizedDescription)
        }
    }

    private func updateTransferItem(_ item: TransferItem) {
        guard var plan = transferPlan,
              let index = plan.items.firstIndex(where: { $0.id == item.id }) else { return }
        plan.items[index] = item
        transferPlan = plan
        transferProgressText = "\(item.sourceURL.lastPathComponent) · \(Int(item.progress * 100))%"
    }

    func clearTransferPlan() {
        transferPlan = nil
        transferReport = nil
        transferProgressText = ""
    }

    func revealDestination(for kind: MediaKind) {
        let url = destination(for: kind)
        if FileManager.default.fileExists(atPath: url.path) {
            NSWorkspace.shared.activateFileViewerSelecting([url])
        } else {
            NSWorkspace.shared.open(url.deletingLastPathComponent())
        }
    }

    func backupFolder() {
        let panel = NSOpenPanel()
        panel.title = "Back up a folder"
        panel.message = "The app copies the folder, hashes every file and writes a manifest. It does not delete or change the source."
        panel.prompt = "Back up"
        panel.canChooseFiles = false
        panel.canChooseDirectories = true
        guard panel.runModal() == .OK, let url = panel.url else { return }
        Task {
            do {
                let folder = try await backupService.backup(urls: [url], label: url.lastPathComponent)
                backupVerified = true
                record("Backup verified", detail: folder.lastPathComponent, symbol: "externaldrive.badge.checkmark", tint: HColor.moss)
                alert = AppAlert(title: "Backup complete", message: "The backup and its SHA-256 manifest are in \(folder.path). Read-back verification passed for every file.")
            } catch {
                alert = AppAlert(title: "Backup failed", message: error.localizedDescription)
            }
        }
    }

    // MARK: - Settings

    func saveSettings() {
        do {
            try JSONStore.save(settings, to: AppPaths.settingsURL)
        } catch {
            alert = AppAlert(title: "Settings not saved", message: error.localizedDescription)
        }
    }

    // MARK: - Persistence and activity

    private func scheduleSave() {
        let value = snapshot
        saveTask?.cancel()
        saveTask = Task { [repository] in
            try? await Task.sleep(nanoseconds: 250_000_000)
            guard !Task.isCancelled else { return }
            try? repository.save(value)
        }
    }

    private func record(_ title: String, detail: String, symbol: String, tint: Color) {
        activities.insert(ActivityEntry(title: title, detail: detail, date: Date(), symbol: symbol, tint: tint), at: 0)
        if activities.count > 24 { activities.removeLast(activities.count - 24) }
    }

    private func removeDemoContentIfNeeded() {
        guard snapshot.tracks.contains(where: \.isDemo) else { return }
        snapshot.tracks.removeAll(where: \.isDemo)
        snapshot.podcasts.removeAll(where: \.isDemo)
        snapshot.audiobooks.removeAll(where: \.isDemo)
        snapshot.scrobbles.removeAll(where: \.isDemo)
        snapshot.albumAnnotations = snapshot.albumAnnotations.filter { key, _ in
            snapshot.tracks.contains(where: { $0.albumKey == key })
        }
        snapshot.artistAnnotations = snapshot.artistAnnotations.filter { key, _ in
            snapshot.tracks.contains(where: { $0.artistKey == key })
        }
        settings.demoLibraryEnabled = false
        saveSettings()
    }
}
