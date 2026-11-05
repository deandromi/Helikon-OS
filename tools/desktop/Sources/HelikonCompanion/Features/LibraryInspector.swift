import AppKit
import CompanionCore
import SwiftUI

struct TrackInspector: View {
    @EnvironmentObject private var model: AppModel
    let track: Track
    let onEdit: () -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: 18) {
            HStack {
                EyebrowLabel(text: "Track")
                Spacer()
                if track.isDemo { Tag(text: "Sample", color: HColor.ochre) }
            }
            CoverArt(
                title: track.displayAlbum,
                artist: track.displayArtist,
                artworkPath: track.artworkPath,
                size: 196,
                corner: 4
            )
            VStack(alignment: .leading, spacing: 5) {
                Text(track.displayTitle)
                    .font(HFont.display(23))
                    .foregroundStyle(HColor.ink)
                    .fixedSize(horizontal: false, vertical: true)
                Text(track.displayArtist)
                    .font(HFont.body(12.5, weight: .medium))
                    .foregroundStyle(HColor.inkSoft)
                Text(track.displayAlbum)
                    .font(HFont.body(11))
                    .foregroundStyle(HColor.muted)
            }
            StarRatingView(rating: track.rating, interactive: true) { value in
                model.setRating(value, for: track)
            }
            if !track.missingFields.isEmpty {
                VStack(alignment: .leading, spacing: 7) {
                    EyebrowLabel(text: "Missing")
                    FlowLayout(spacing: 6) {
                        ForEach(track.missingFields, id: \.rawValue) { field in
                            Tag(text: field.title, color: HColor.ochre)
                        }
                    }
                }
            }
            VStack(alignment: .leading, spacing: 8) {
                KeyValue(key: "Format", value: track.format.isEmpty ? "—" : track.format)
                KeyValue(key: "Length", value: track.durationText)
                KeyValue(key: "Size", value: track.fileSizeText)
                KeyValue(key: "Source", value: track.metadataSource ?? "Embedded tags")
                KeyValue(key: "Path", value: track.relativePath ?? track.fileURL.lastPathComponent)
            }
            if let notes = track.notes, !notes.isEmpty {
                VStack(alignment: .leading, spacing: 5) {
                    EyebrowLabel(text: "Notes")
                    Text(notes)
                        .font(HFont.body(11.5))
                        .foregroundStyle(HColor.muted)
                        .lineSpacing(3)
                }
            }
            Hairline()
            VStack(alignment: .leading, spacing: 8) {
                Button {
                    if track.isDemo {
                        model.alert = AppAlert(title: "Sample record", message: "Search works, but there is no audio file behind this sample.")
                    }
                    model.searchMetadata(for: track)
                } label: {
                    Label("Find metadata", systemImage: "sparkle.magnifyingglass")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(PrimaryButtonStyle())

                HStack(spacing: 8) {
                    Button {
                        onEdit()
                    } label: {
                        Label("Edit", systemImage: "pencil")
                            .frame(maxWidth: .infinity)
                    }
                    .buttonStyle(SecondaryButtonStyle())
                    Button {
                        model.addScrobble(for: track)
                    } label: {
                        Label("Scrobble", systemImage: "waveform.path.ecg")
                            .frame(maxWidth: .infinity)
                    }
                    .buttonStyle(SecondaryButtonStyle())
                    .disabled(track.isDemo)
                }

                Button {
                    NSWorkspace.shared.activateFileViewerSelecting([track.fileURL])
                } label: {
                    Label("Reveal in Finder", systemImage: "folder")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(SecondaryButtonStyle())
                .disabled(track.isDemo)

                Button(role: .destructive) {
                    model.removeTrack(track)
                } label: {
                    Text("Remove from companion library")
                        .font(HFont.body(11))
                        .foregroundStyle(HColor.error)
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.plain)
                .disabled(track.isDemo)
            }
        }
        .padding(20)
    }
}

struct AlbumInspector: View {
    @EnvironmentObject private var model: AppModel
    let album: AlbumSummary
    @State private var description = ""
    @State private var loadedAlbumID = ""

    private var albumTracks: [Track] {
        model.snapshot.tracks
            .filter { $0.albumKey == album.id }
            .sorted {
                if ($0.discNumber ?? 0) != ($1.discNumber ?? 0) { return ($0.discNumber ?? 0) < ($1.discNumber ?? 0) }
                return ($0.trackNumber ?? 0) < ($1.trackNumber ?? 0)
            }
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 18) {
            HStack {
                EyebrowLabel(text: "Album")
                Spacer()
                if album.isDemo { Tag(text: "Sample", color: HColor.ochre) }
            }
            CoverArt(
                title: album.title,
                artist: album.artist,
                artworkPath: album.artworkPath,
                size: 196,
                corner: 4
            )
            VStack(alignment: .leading, spacing: 5) {
                Text(album.title)
                    .font(HFont.display(23))
                    .foregroundStyle(HColor.ink)
                Text(album.artist)
                    .font(HFont.body(12.5, weight: .medium))
                    .foregroundStyle(HColor.inkSoft)
                Text([album.year, album.genre].compactMap { $0 }.joined(separator: " · "))
                    .font(HFont.body(10.5))
                    .foregroundStyle(HColor.muted)
            }
            StarRatingView(rating: album.rating, interactive: true) { value in
                model.setAlbumRating(value, albumID: album.id)
            }
            VStack(alignment: .leading, spacing: 6) {
                EyebrowLabel(text: "Album note")
                TextEditor(text: $description)
                    .font(HFont.body(11.5))
                    .foregroundStyle(HColor.inkSoft)
                    .scrollContentBackground(.hidden)
                    .padding(8)
                    .frame(minHeight: 110)
                    .background(HColor.paperLight)
                    .overlay(
                        RoundedRectangle(cornerRadius: 6, style: .continuous)
                            .stroke(HColor.rule, lineWidth: 1)
                    )
                    .clipShape(RoundedRectangle(cornerRadius: 6, style: .continuous))
                Button("Save note") {
                    model.setAlbumDescription(description, albumID: album.id)
                    model.flash("Album note saved.")
                }
                .buttonStyle(SecondaryButtonStyle())
            }
            VStack(alignment: .leading, spacing: 7) {
                HStack {
                    EyebrowLabel(text: "Tracks")
                    Spacer()
                    Text("\(album.trackCount) · \(album.durationText)")
                        .font(HFont.mono(9.5))
                        .foregroundStyle(HColor.muted)
                }
                ForEach(albumTracks) { track in
                    HStack(spacing: 8) {
                        Text(track.trackNumber.map(String.init) ?? "—")
                            .font(HFont.mono(9))
                            .foregroundStyle(HColor.faint)
                            .frame(width: 20, alignment: .trailing)
                        Text(track.displayTitle)
                            .font(HFont.body(11))
                            .foregroundStyle(HColor.inkSoft)
                            .lineLimit(1)
                        Spacer()
                        Text(track.durationText)
                            .font(HFont.mono(9.5))
                            .foregroundStyle(HColor.muted)
                    }
                    .padding(.vertical, 3)
                }
            }
            Hairline()
            Button {
                model.searchMetadata(for: album)
            } label: {
                Label("Find album metadata", systemImage: "sparkle.magnifyingglass")
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(PrimaryButtonStyle())
            Button {
                if let first = albumTracks.first {
                    NSWorkspace.shared.activateFileViewerSelecting([first.fileURL])
                }
            } label: {
                Label("Reveal in Finder", systemImage: "folder")
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(SecondaryButtonStyle())
            .disabled(album.isDemo)
        }
        .padding(20)
        .onAppear { syncDescription() }
        .onChange(of: album.id) { _, _ in syncDescription() }
        .onChange(of: album.description) { _, newValue in
            if description != newValue { description = newValue }
        }
    }

    private func syncDescription() {
        guard loadedAlbumID != album.id else { return }
        loadedAlbumID = album.id
        description = album.description
    }
}

struct ArtistInspector: View {
    @EnvironmentObject private var model: AppModel
    let artist: ArtistSummary
    @State private var biography = ""
    @State private var loadedArtistID = ""

    var body: some View {
        VStack(alignment: .leading, spacing: 18) {
            HStack {
                EyebrowLabel(text: "Artist")
                Spacer()
                if artist.isDemo { Tag(text: "Sample", color: HColor.ochre) }
            }
            CoverArt(title: artist.name, artist: "", size: 196, corner: 98)
            VStack(alignment: .leading, spacing: 5) {
                Text(artist.name)
                    .font(HFont.display(23))
                    .foregroundStyle(HColor.ink)
                Text("\(artist.albumCount) albums · \(artist.trackCount) tracks")
                    .font(HFont.body(11))
                    .foregroundStyle(HColor.muted)
                if let country = artist.country, !country.isEmpty {
                    Text(country)
                        .font(HFont.body(10.5))
                        .foregroundStyle(HColor.faint)
                }
            }
            VStack(alignment: .leading, spacing: 6) {
                EyebrowLabel(text: "Biography")
                TextEditor(text: $biography)
                    .font(HFont.body(11.5))
                    .foregroundStyle(HColor.inkSoft)
                    .scrollContentBackground(.hidden)
                    .padding(8)
                    .frame(minHeight: 150)
                    .background(HColor.paperLight)
                    .overlay(
                        RoundedRectangle(cornerRadius: 6, style: .continuous)
                            .stroke(HColor.rule, lineWidth: 1)
                    )
                    .clipShape(RoundedRectangle(cornerRadius: 6, style: .continuous))
                Button("Save biography") {
                    model.setArtistBiography(biography, artistID: artist.id)
                    model.flash("Artist biography saved.")
                }
                .buttonStyle(SecondaryButtonStyle())
            }
            Hairline()
            Button {
                model.searchMetadata(for: artist)
            } label: {
                Label("Find artist metadata", systemImage: "sparkle.magnifyingglass")
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(PrimaryButtonStyle())
        }
        .padding(20)
        .onAppear { syncBiography() }
        .onChange(of: artist.id) { _, _ in syncBiography() }
        .onChange(of: artist.biography) { _, newValue in
            if biography != newValue { biography = newValue }
        }
    }

    private func syncBiography() {
        guard loadedArtistID != artist.id else { return }
        loadedArtistID = artist.id
        biography = artist.biography
    }
}

struct StarRatingView: View {
    let rating: Int
    var interactive = false
    var onChange: ((Int) -> Void)?

    var body: some View {
        HStack(spacing: 5) {
            ForEach(1...5, id: \.self) { index in
                Button {
                    guard interactive else { return }
                    onChange?(rating == index ? 0 : index)
                } label: {
                    Image(systemName: index <= rating ? "star.fill" : "star")
                        .font(.system(size: 11, weight: .regular))
                        .foregroundStyle(index <= rating ? HColor.ochre : HColor.faint)
                }
                .buttonStyle(.plain)
                .disabled(!interactive)
            }
            if rating == 0 {
                Text("Not rated")
                    .font(HFont.body(9.5))
                    .foregroundStyle(HColor.faint)
                    .padding(.leading, 3)
            }
        }
    }
}

// MARK: - Edit sheet

struct TrackEditSheet: View {
    @EnvironmentObject private var model: AppModel
    @Environment(\.dismiss) private var dismiss
    let track: Track

    @State private var title: String
    @State private var artist: String
    @State private var album: String
    @State private var albumArtist: String
    @State private var year: String
    @State private var genre: String
    @State private var notes: String

    init(track: Track) {
        self.track = track
        _title = State(initialValue: track.title)
        _artist = State(initialValue: track.artist)
        _album = State(initialValue: track.album)
        _albumArtist = State(initialValue: track.albumArtist)
        _year = State(initialValue: track.year ?? "")
        _genre = State(initialValue: track.genre ?? "")
        _notes = State(initialValue: track.notes ?? "")
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 18) {
            HStack(alignment: .top) {
                VStack(alignment: .leading, spacing: 4) {
                    EyebrowLabel(text: "Manual edit")
                    Text("Track tags")
                        .font(HFont.display(26))
                        .foregroundStyle(HColor.ink)
                }
                Spacer()
                Button("Cancel") { dismiss() }
                    .buttonStyle(QuietButtonStyle())
            }
            HStack(spacing: 12) {
                CoverArt(title: album.isEmpty ? track.displayAlbum : album, artist: artist, artworkPath: track.artworkPath, size: 72, corner: 3)
                VStack(alignment: .leading, spacing: 4) {
                    Text(track.fileURL.lastPathComponent)
                        .font(HFont.body(11, weight: .medium))
                        .foregroundStyle(HColor.inkSoft)
                    Text("\(track.format) · \(track.fileSizeText) · \(track.durationText)")
                        .font(HFont.mono(9.5))
                        .foregroundStyle(HColor.muted)
                    Text("Changes are stored in the companion library. The audio file is not rewritten.")
                        .font(HFont.body(9.5))
                        .foregroundStyle(HColor.faint)
                }
            }
            HStack(spacing: 12) {
                FilledTagField(label: "Title", text: $title)
                FilledTagField(label: "Artist", text: $artist)
            }
            HStack(spacing: 12) {
                FilledTagField(label: "Album", text: $album)
                FilledTagField(label: "Album artist", text: $albumArtist)
            }
            HStack(spacing: 12) {
                FilledTagField(label: "Year", text: $year, placeholder: "2026")
                FilledTagField(label: "Genre", text: $genre)
            }
            VStack(alignment: .leading, spacing: 6) {
                EyebrowLabel(text: "Notes")
                TextEditor(text: $notes)
                    .font(HFont.body(11.5))
                    .scrollContentBackground(.hidden)
                    .padding(8)
                    .frame(height: 92)
                    .background(HColor.paperLight)
                    .overlay(
                        RoundedRectangle(cornerRadius: 6, style: .continuous)
                            .stroke(HColor.rule, lineWidth: 1)
                    )
                    .clipShape(RoundedRectangle(cornerRadius: 6, style: .continuous))
            }
            Spacer(minLength: 0)
            HStack {
                Spacer()
                Button("Cancel") { dismiss() }
                    .buttonStyle(SecondaryButtonStyle())
                Button("Save changes") {
                    model.updateTrack(
                        track.id,
                        title: title,
                        artist: artist,
                        album: album,
                        albumArtist: albumArtist,
                        year: year,
                        genre: genre,
                        notes: notes
                    )
                    dismiss()
                }
                .buttonStyle(PrimaryButtonStyle())
            }
        }
        .padding(24)
        .frame(width: 560, height: 520)
        .background(HColor.paper)
    }
}

// MARK: - Flow layout

struct FlowLayout: Layout {
    var spacing: CGFloat = 6

    func sizeThatFits(proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) -> CGSize {
        let width = proposal.width ?? 280
        var x: CGFloat = 0
        var y: CGFloat = 0
        var rowHeight: CGFloat = 0
        for subview in subviews {
            let size = subview.sizeThatFits(.unspecified)
            if x + size.width > width && x > 0 {
                x = 0
                y += rowHeight + spacing
                rowHeight = 0
            }
            x += size.width + spacing
            rowHeight = max(rowHeight, size.height)
        }
        return CGSize(width: width, height: y + rowHeight)
    }

    func placeSubviews(in bounds: CGRect, proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) {
        var x = bounds.minX
        var y = bounds.minY
        var rowHeight: CGFloat = 0
        for subview in subviews {
            let size = subview.sizeThatFits(.unspecified)
            if x + size.width > bounds.maxX && x > bounds.minX {
                x = bounds.minX
                y += rowHeight + spacing
                rowHeight = 0
            }
            subview.place(at: CGPoint(x: x, y: y), proposal: ProposedViewSize(size))
            x += size.width + spacing
            rowHeight = max(rowHeight, size.height)
        }
    }
}
