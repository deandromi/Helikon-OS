import AppKit
import CompanionCore
import SwiftUI

enum LibraryMode: String, CaseIterable, Identifiable {
    case tracks
    case albums
    case artists
    case missing

    var id: String { rawValue }

    var title: String {
        switch self {
        case .tracks: return "Tracks"
        case .albums: return "Albums"
        case .artists: return "Artists"
        case .missing: return "Needs metadata"
        }
    }
}

struct LibraryView: View {
    @EnvironmentObject private var model: AppModel
    @State private var mode: LibraryMode = .tracks
    @State private var query = ""
    @State private var selectedTrackID: UUID?
    @State private var selectedAlbumID: String?
    @State private var selectedArtistID: String?
    @State private var editingTrack: Track?

    private var filteredTracks: [Track] {
        let tracks = mode == .missing ? model.snapshot.tracksMissingMetadata : model.snapshot.tracks
        guard !query.isEmpty else { return tracks }
        return tracks.filter { track in
            [track.displayTitle, track.displayArtist, track.displayAlbum, track.format]
                .contains { $0.localizedCaseInsensitiveContains(query) }
        }
    }

    private var filteredAlbums: [AlbumSummary] {
        guard !query.isEmpty else { return model.snapshot.albums }
        return model.snapshot.albums.filter {
            $0.title.localizedCaseInsensitiveContains(query) || $0.artist.localizedCaseInsensitiveContains(query)
        }
    }

    private var filteredArtists: [ArtistSummary] {
        guard !query.isEmpty else { return model.snapshot.artists }
        return model.snapshot.artists.filter { $0.name.localizedCaseInsensitiveContains(query) }
    }

    private var selectedTrack: Track? {
        guard let selectedTrackID else { return nil }
        return model.snapshot.tracks.first(where: { $0.id == selectedTrackID })
    }

    private var selectedAlbum: AlbumSummary? {
        guard let selectedAlbumID else { return nil }
        return model.snapshot.albums.first(where: { $0.id == selectedAlbumID })
    }

    private var selectedArtist: ArtistSummary? {
        guard let selectedArtistID else { return nil }
        return model.snapshot.artists.first(where: { $0.id == selectedArtistID })
    }

    private var hasInspector: Bool {
        (mode == .tracks || mode == .missing) && selectedTrack != nil
            || mode == .albums && selectedAlbum != nil
            || mode == .artists && selectedArtist != nil
    }

    var body: some View {
        VStack(spacing: 0) {
            header
            controls
            Hairline()
            HStack(spacing: 0) {
                results
                if hasInspector {
                    VerticalHairline()
                    inspector
                        .frame(width: 322)
                        .background(HColor.paperLight.opacity(0.45))
                }
            }
        }
        .background(HColor.paper)
        .sheet(item: $editingTrack) { track in
            TrackEditSheet(track: track)
                .environmentObject(model)
        }
        .onChange(of: mode) { _, _ in
            selectedTrackID = nil
            selectedAlbumID = nil
            selectedArtistID = nil
        }
    }

    private var header: some View {
        HStack(alignment: .top, spacing: 20) {
            SectionTitle(
                "Library",
                kicker: "The library desk",
                subtitle: "Tracks, albums and artists read from the mounted volume or a folder you choose. Nothing plays here; this is the desk, not the listener."
            )
            Spacer(minLength: 20)
            VStack(alignment: .trailing, spacing: 8) {
                Button {
                    model.chooseAudioFiles()
                } label: {
                    Label("Add music", systemImage: "plus")
                }
                .buttonStyle(PrimaryButtonStyle())
                Button {
                    model.scanSelectedDevice()
                } label: {
                    Text("Scan volume")
                }
                .buttonStyle(QuietButtonStyle())
                .disabled(model.selectedDevice == nil)
            }
            .padding(.top, 4)
        }
        .padding(.horizontal, 32)
        .padding(.top, 30)
        .padding(.bottom, 20)
    }

    private var controls: some View {
        HStack(spacing: 16) {
            HStack(spacing: 0) {
                ForEach(LibraryMode.allCases) { item in
                    Button {
                        mode = item
                    } label: {
                        HStack(spacing: 6) {
                            Text(item.title)
                                .font(HFont.body(11, weight: mode == item ? .medium : .regular))
                            if item == .missing, model.missingMetadataCount > 0 {
                                Text("\(model.missingMetadataCount)")
                                    .font(HFont.mono(9))
                                    .foregroundStyle(mode == item ? HColor.paperLight.opacity(0.65) : HColor.ochre)
                            }
                        }
                        .foregroundStyle(mode == item ? HColor.paperLight : HColor.inkSoft)
                        .padding(.horizontal, 12)
                        .padding(.vertical, 7)
                        .background(mode == item ? HColor.ink : Color.clear)
                    }
                    .buttonStyle(.plain)
                }
            }
            .overlay(
                RoundedRectangle(cornerRadius: 7, style: .continuous)
                    .stroke(HColor.rule, lineWidth: 1)
            )
            .clipShape(RoundedRectangle(cornerRadius: 7, style: .continuous))

            SearchField(placeholder: "Search title, artist, album…", text: $query)
                .frame(maxWidth: 290)

            Spacer()
            Text(resultCount)
                .font(HFont.mono(10))
                .foregroundStyle(HColor.muted)
        }
        .padding(.horizontal, 32)
        .padding(.bottom, 14)
    }

    private var resultCount: String {
        switch mode {
        case .tracks, .missing:
            let count = filteredTracks.count
            return "\(count) track\(count == 1 ? "" : "s")"
        case .albums:
            let count = filteredAlbums.count
            return "\(count) album\(count == 1 ? "" : "s")"
        case .artists:
            let count = filteredArtists.count
            return "\(count) artist\(count == 1 ? "" : "s")"
        }
    }

    @ViewBuilder
    private var results: some View {
        if model.snapshot.tracks.isEmpty {
            ScrollView {
                EmptyPanel(
                    icon: "music.note.list",
                    title: "The library is empty",
                    message: "Connect and scan a volume, add a folder, or load the sample library to explore the app.",
                    actionTitle: "Add music",
                    action: { model.chooseAudioFiles() }
                )
                .padding(32)
            }
            .frame(maxWidth: .infinity)
        } else {
            ScrollView {
                switch mode {
                case .tracks, .missing:
                    trackList
                case .albums:
                    albumGrid
                case .artists:
                    artistList
                }
            }
            .frame(maxWidth: .infinity)
        }
    }

    private var trackList: some View {
        LazyVStack(spacing: 0) {
            HStack(spacing: 12) {
                Text("Title")
                    .frame(maxWidth: .infinity, alignment: .leading)
                Text("Album")
                    .frame(width: 160, alignment: .leading)
                Text("Length")
                    .frame(width: 58, alignment: .trailing)
                Text("Tags")
                    .frame(width: 68, alignment: .trailing)
            }
            .font(HFont.label(9.5, weight: .medium))
            .tracking(0.3)
            .foregroundStyle(HColor.muted)
            .padding(.horizontal, 24)
            .padding(.vertical, 9)
            .background(HColor.paperDeep.opacity(0.5))
            Hairline()
            ForEach(filteredTracks) { track in
                TrackLibraryRow(
                    track: track,
                    selected: selectedTrackID == track.id,
                    showAlbum: true
                )
                .onTapGesture {
                    selectedTrackID = track.id
                    selectedAlbumID = nil
                    selectedArtistID = nil
                }
                .contextMenu {
                    Button("Search metadata…") { model.searchMetadata(for: track) }
                    Button("Queue a scrobble") { model.addScrobble(for: track) }
                    Divider()
                    Button("Edit tags…") { editingTrack = track }
                    Button("Reveal in Finder") {
                        NSWorkspace.shared.activateFileViewerSelecting([track.fileURL])
                    }
                    .disabled(track.isDemo)
                    Divider()
                    Button("Remove from library", role: .destructive) { model.removeTrack(track) }
                }
                Hairline()
            }
        }
        .padding(.bottom, 20)
    }

    private var albumGrid: some View {
        LazyVGrid(
            columns: [GridItem(.adaptive(minimum: 148, maximum: 190), spacing: 20)],
            alignment: .leading,
            spacing: 24
        ) {
            ForEach(filteredAlbums) { album in
                Button {
                    selectedAlbumID = album.id
                    selectedTrackID = nil
                    selectedArtistID = nil
                } label: {
                    VStack(alignment: .leading, spacing: 9) {
                        CoverArt(
                            title: album.title,
                            artist: album.artist,
                            artworkPath: album.artworkPath,
                            size: 158,
                            corner: 3
                        )
                        .overlay(
                            RoundedRectangle(cornerRadius: 3, style: .continuous)
                                .stroke(selectedAlbumID == album.id ? HColor.ink : Color.clear, lineWidth: 2)
                                .padding(-4)
                        )
                        Text(album.title)
                            .font(HFont.body(12, weight: .medium))
                            .foregroundStyle(HColor.ink)
                            .lineLimit(1)
                        Text(album.subtitle)
                            .font(HFont.body(10.5))
                            .foregroundStyle(HColor.muted)
                            .lineLimit(1)
                    }
                }
                .buttonStyle(.plain)
                .contextMenu {
                    Button("Search album metadata…") { model.searchMetadata(for: album) }
                    Button("Reveal in Finder") {
                        if let first = model.snapshot.tracks.first(where: { $0.albumKey == album.id }) {
                            NSWorkspace.shared.activateFileViewerSelecting([first.fileURL])
                        }
                    }
                    .disabled(album.isDemo)
                }
            }
        }
        .padding(.horizontal, 28)
        .padding(.vertical, 22)
    }

    private var artistList: some View {
        LazyVStack(spacing: 0) {
            ForEach(filteredArtists) { artist in
                Button {
                    selectedArtistID = artist.id
                    selectedTrackID = nil
                    selectedAlbumID = nil
                } label: {
                    HStack(spacing: 14) {
                        CoverArt(title: artist.name, artist: "", size: 44, corner: 22)
                        VStack(alignment: .leading, spacing: 3) {
                            Text(artist.name)
                                .font(HFont.body(13, weight: .medium))
                                .foregroundStyle(HColor.ink)
                            Text("\(artist.albumCount) albums · \(artist.trackCount) tracks")
                                .font(HFont.body(10.5))
                                .foregroundStyle(HColor.muted)
                        }
                        Spacer()
                        if artist.biography.isEmpty {
                            Tag(text: "No bio", color: HColor.ochre)
                        } else {
                            Text("Biography")
                                .font(HFont.body(9.5))
                                .foregroundStyle(HColor.faint)
                        }
                        Image(systemName: "chevron.right")
                            .font(.system(size: 9, weight: .semibold))
                            .foregroundStyle(HColor.faint)
                    }
                    .padding(.horizontal, 24)
                    .padding(.vertical, 12)
                    .background(selectedArtistID == artist.id ? HColor.paperDeep.opacity(0.55) : Color.clear)
                    .contentShape(Rectangle())
                }
                .buttonStyle(.plain)
                Hairline()
            }
        }
        .padding(.bottom, 20)
    }

    @ViewBuilder
    private var inspector: some View {
        ScrollView {
            if let track = selectedTrack {
                TrackInspector(track: track) {
                    editingTrack = track
                }
            } else if let album = selectedAlbum {
                AlbumInspector(album: album)
            } else if let artist = selectedArtist {
                ArtistInspector(artist: artist)
            }
        }
    }
}

// MARK: - Rows

struct TrackLibraryRow: View {
    let track: Track
    let selected: Bool
    var showAlbum: Bool = true
    @State private var hovering = false

    var body: some View {
        HStack(spacing: 12) {
            CoverArt(
                title: track.displayAlbum,
                artist: track.displayArtist,
                artworkPath: track.artworkPath,
                size: 34,
                corner: 2
            )
            VStack(alignment: .leading, spacing: 3) {
                Text(track.displayTitle)
                    .font(HFont.body(12, weight: .medium))
                    .foregroundStyle(HColor.ink)
                    .lineLimit(1)
                Text(track.displayArtist)
                    .font(HFont.body(10.5))
                    .foregroundStyle(HColor.muted)
                    .lineLimit(1)
            }
            .frame(maxWidth: .infinity, alignment: .leading)
            if showAlbum {
                Text(track.displayAlbum)
                    .font(HFont.body(10.5))
                    .foregroundStyle(HColor.muted)
                    .lineLimit(1)
                    .frame(width: 160, alignment: .leading)
            }
            Text(track.durationText)
                .font(HFont.mono(10))
                .foregroundStyle(HColor.muted)
                .frame(width: 58, alignment: .trailing)
            HStack {
                if track.missingFields.isEmpty {
                    Image(systemName: "checkmark")
                        .font(.system(size: 9, weight: .semibold))
                        .foregroundStyle(HColor.moss)
                    Text("Complete")
                        .font(HFont.body(9.5))
                        .foregroundStyle(HColor.muted)
                } else if track.missingFields == [.artwork] {
                    Text("Artwork")
                        .font(HFont.body(9.5, weight: .medium))
                        .foregroundStyle(HColor.ochre)
                } else {
                    Text("\(track.missingFields.count) missing")
                        .font(HFont.body(9.5, weight: .medium))
                        .foregroundStyle(HColor.ochre)
                }
            }
            .frame(width: 68, alignment: .trailing)
        }
        .padding(.horizontal, 24)
        .padding(.vertical, 9)
        .background(
            selected
                ? HColor.paperDeep.opacity(0.8)
                : (hovering ? HColor.paperLight.opacity(0.75) : Color.clear)
        )
        .contentShape(Rectangle())
        .onHover { hovering = $0 }
    }
}
