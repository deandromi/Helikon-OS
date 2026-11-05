import SwiftUI
import CompanionCore

struct MetadataView: View {
    @EnvironmentObject private var model: AppModel
    @State private var entity: MetadataEntity = .track
    @State private var title = ""
    @State private var artist = ""
    @State private var album = ""

    private var albumsWithoutNotes: Int {
        model.snapshot.albums.filter { $0.description.isEmpty }.count
    }

    private var artistsWithoutBio: Int {
        model.snapshot.artists.filter { $0.biography.isEmpty }.count
    }

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 24) {
                SectionTitle(
                    "Metadata",
                    kicker: "Sources and tags",
                    subtitle: "Search MusicBrainz and Apple, then keep the result in the companion library. A candidate never overwrites a file until you choose Apply."
                )
                summary
                searchComposer
                missingQueue
                methodNote
            }
            .padding(.horizontal, 32)
            .padding(.vertical, 30)
        }
        .background(HColor.paper)
    }

    private var summary: some View {
        HStack(spacing: 0) {
            MetadataStat(
                value: "\(model.missingMetadataCount)",
                label: "Tracks missing tags",
                tone: HColor.ochre
            )
            MetadataStatDivider()
            MetadataStat(
                value: "\(albumsWithoutNotes)",
                label: "Albums without a note",
                tone: HColor.blue
            )
            MetadataStatDivider()
            MetadataStat(
                value: "\(artistsWithoutBio)",
                label: "Artists without a bio",
                tone: HColor.moss
            )
            MetadataStatDivider()
            VStack(alignment: .leading, spacing: 5) {
                Text(model.settings.metadataProviders.map(\.title).sorted().joined(separator: " · "))
                    .font(HFont.display(21))
                    .foregroundStyle(HColor.ink)
                Text("Active providers")
                    .font(HFont.body(10.5, weight: .medium))
                    .foregroundStyle(HColor.inkSoft)
                Text("Change in Settings")
                    .font(HFont.body(10))
                    .foregroundStyle(HColor.muted)
            }
            .frame(maxWidth: .infinity, alignment: .leading)
            .padding(.horizontal, 20)
            .padding(.vertical, 14)
        }
        .background(HColor.paperLight.opacity(0.45))
        .overlay(
            RoundedRectangle(cornerRadius: 10, style: .continuous)
                .stroke(HColor.rule, lineWidth: 1)
        )
        .clipShape(RoundedRectangle(cornerRadius: 10, style: .continuous))
    }

    private var searchComposer: some View {
        VStack(alignment: .leading, spacing: 14) {
            HStack {
                Text("Search by hand")
                    .font(HFont.display(21))
                    .foregroundStyle(HColor.ink)
                Spacer()
                if !model.settings.networkEnabled {
                    Tag(text: "Network off", color: HColor.error)
                }
            }
            HStack(spacing: 0) {
                ForEach([MetadataEntity.track, .album, .artist], id: \.self) { item in
                    Button {
                        entity = item
                    } label: {
                        Text(item.rawValue.capitalized)
                            .font(HFont.body(11, weight: entity == item ? .medium : .regular))
                            .foregroundStyle(entity == item ? HColor.paperLight : HColor.inkSoft)
                            .padding(.horizontal, 13)
                            .padding(.vertical, 7)
                            .background(entity == item ? HColor.ink : Color.clear)
                    }
                    .buttonStyle(.plain)
                }
            }
            .overlay(
                RoundedRectangle(cornerRadius: 7, style: .continuous)
                    .stroke(HColor.rule, lineWidth: 1)
            )
            .clipShape(RoundedRectangle(cornerRadius: 7, style: .continuous))
            .frame(maxWidth: .infinity, alignment: .leading)

            HStack(spacing: 12) {
                FilledTagField(label: "Title", text: $title, placeholder: entity == .artist ? "Artist name" : "Album or track")
                FilledTagField(label: "Artist", text: $artist, placeholder: "Optional")
                FilledTagField(label: "Album", text: $album, placeholder: "Optional")
            }
            HStack {
                Text("Searches are explicit. The app does not crawl the library on launch.")
                    .font(HFont.body(10))
                    .foregroundStyle(HColor.faint)
                Spacer()
                Button {
                    model.searchMetadata(title: title, artist: artist, album: album, entity: entity)
                } label: {
                    Label("Search metadata", systemImage: "magnifyingglass")
                }
                .buttonStyle(PrimaryButtonStyle())
                .disabled(title.isEmpty && artist.isEmpty)
            }
        }
        .padding(18)
        .background(HColor.paperLight)
        .overlay(
            RoundedRectangle(cornerRadius: 10, style: .continuous)
                .stroke(HColor.rule, lineWidth: 1)
        )
        .clipShape(RoundedRectangle(cornerRadius: 10, style: .continuous))
    }

    private var missingQueue: some View {
        VStack(alignment: .leading, spacing: 0) {
            HStack {
                Text("The metadata queue")
                    .font(HFont.display(21))
                    .foregroundStyle(HColor.ink)
                Spacer()
                Text("\(model.snapshot.tracksMissingMetadata.count) \(model.snapshot.tracksMissingMetadata.count == 1 ? "track" : "tracks")")
                    .font(HFont.mono(10))
                    .foregroundStyle(HColor.muted)
            }
            .padding(.bottom, 12)

            if model.snapshot.tracksMissingMetadata.isEmpty {
                EmptyPanel(
                    icon: "checkmark.seal",
                    title: "No missing tags",
                    message: "Every track in the companion library has a title, artist, album and some artwork."
                )
            } else {
                ForEach(Array(model.snapshot.tracksMissingMetadata.prefix(12))) { track in
                    HStack(spacing: 12) {
                        CoverArt(
                            title: track.displayAlbum,
                            artist: track.displayArtist,
                            artworkPath: track.artworkPath,
                            size: 36,
                            corner: 2
                        )
                        VStack(alignment: .leading, spacing: 3) {
                            Text(track.displayTitle)
                                .font(HFont.body(12, weight: .medium))
                                .foregroundStyle(HColor.ink)
                            Text(track.displayArtist == "Unknown artist" ? track.relativePath ?? "Unknown artist" : track.displayArtist)
                                .font(HFont.body(10.5))
                                .foregroundStyle(HColor.muted)
                        }
                        Spacer()
                        HStack(spacing: 5) {
                            ForEach(track.missingFields.prefix(3), id: \.rawValue) { field in
                                Tag(text: field.title, color: HColor.ochre)
                            }
                        }
                        Button("Find") {
                            model.searchMetadata(for: track)
                        }
                        .buttonStyle(SecondaryButtonStyle())
                    }
                    .padding(.vertical, 9)
                    Hairline()
                }
                if model.snapshot.tracksMissingMetadata.count > 12 {
                    Text("Showing 12 of \(model.snapshot.tracksMissingMetadata.count). Use the Library tab for the whole queue.")
                        .font(HFont.body(10))
                        .foregroundStyle(HColor.faint)
                        .padding(.top, 10)
                }
            }
        }
        .padding(18)
        .background(HColor.paperLight)
        .overlay(
            RoundedRectangle(cornerRadius: 10, style: .continuous)
                .stroke(HColor.rule, lineWidth: 1)
        )
        .clipShape(RoundedRectangle(cornerRadius: 10, style: .continuous))
    }

    private var methodNote: some View {
        HStack(alignment: .top, spacing: 14) {
            Image(systemName: "info.circle")
                .font(.system(size: 14, weight: .light))
                .foregroundStyle(HColor.muted)
            VStack(alignment: .leading, spacing: 5) {
                Text("What Apply does")
                    .font(HFont.body(11.5, weight: .semibold))
                    .foregroundStyle(HColor.ink)
                Text("It writes the chosen fields to the companion library, downloads artwork into the app's local cache, and marks the source. It does not silently rewrite your audio files. Manual edits and searched results are both kept for the next transfer or library build.")
                    .font(HFont.body(10.5))
                    .foregroundStyle(HColor.muted)
                    .lineSpacing(3)
            }
            Spacer()
        }
        .padding(16)
        .background(HColor.paperDeep.opacity(0.45))
        .overlay(
            RoundedRectangle(cornerRadius: 8, style: .continuous)
                .stroke(HColor.rule, lineWidth: 1)
        )
        .clipShape(RoundedRectangle(cornerRadius: 8, style: .continuous))
    }
}

private struct MetadataStat: View {
    let value: String
    let label: String
    let tone: Color

    var body: some View {
        VStack(alignment: .leading, spacing: 5) {
            Text(value)
                .font(HFont.display(28))
                .foregroundStyle(HColor.ink)
                .monospacedDigit()
            HStack(spacing: 6) {
                Circle().fill(tone).frame(width: 5, height: 5)
                Text(label)
                    .font(HFont.body(10.5, weight: .medium))
                    .foregroundStyle(HColor.inkSoft)
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(.horizontal, 20)
        .padding(.vertical, 14)
    }
}

private struct MetadataStatDivider: View {
    var body: some View {
        Rectangle().fill(HColor.rule).frame(width: 1, height: 44)
    }
}

// MARK: - Results sheet

struct MetadataResultsSheet: View {
    @EnvironmentObject private var model: AppModel
    @Environment(\.dismiss) private var dismiss
    let contextID: UUID

    private var context: MetadataSearchContext? {
        guard let context = model.metadataSearch, context.id == contextID else { return nil }
        return context
    }

    var body: some View {
        VStack(spacing: 0) {
            header
            Hairline()
            if let context {
                if context.isLoading {
                    loading
                } else {
                    results(context)
                }
            } else {
                Spacer()
                Text("Search closed")
                    .font(HFont.body(12))
                    .foregroundStyle(HColor.muted)
                Spacer()
            }
        }
        .frame(width: 760, height: 640)
        .background(HColor.paper)
    }

    private var header: some View {
        HStack(alignment: .top, spacing: 18) {
            VStack(alignment: .leading, spacing: 5) {
                EyebrowLabel(text: context.map { "\($0.entity.rawValue.capitalized) search" } ?? "Metadata search")
                Text(context?.title ?? "Metadata")
                    .font(HFont.display(27))
                    .foregroundStyle(HColor.ink)
                    .lineLimit(2)
                if let subtitle = context?.subtitle, !subtitle.isEmpty {
                    Text(subtitle)
                        .font(HFont.body(11.5))
                        .foregroundStyle(HColor.muted)
                }
            }
            Spacer()
            Button {
                dismiss()
            } label: {
                Image(systemName: "xmark")
                    .font(.system(size: 11, weight: .semibold))
                    .foregroundStyle(HColor.muted)
                    .frame(width: 26, height: 26)
                    .overlay(Circle().stroke(HColor.rule, lineWidth: 1))
            }
            .buttonStyle(.plain)
        }
        .padding(22)
    }

    private var loading: some View {
        VStack(spacing: 14) {
            Spacer()
            ProgressView()
                .controlSize(.small)
            Text("Searching MusicBrainz and Apple…")
                .font(HFont.body(11.5))
                .foregroundStyle(HColor.muted)
            Spacer()
        }
        .frame(maxWidth: .infinity)
    }

    private func results(_ context: MetadataSearchContext) -> some View {
        VStack(spacing: 0) {
            if !context.warnings.isEmpty {
                HStack(alignment: .top, spacing: 9) {
                    Image(systemName: "exclamationmark.triangle")
                        .font(.system(size: 11))
                        .foregroundStyle(HColor.ochre)
                    Text(context.warnings.joined(separator: "\n"))
                        .font(HFont.body(10))
                        .foregroundStyle(HColor.muted)
                    Spacer()
                }
                .padding(.horizontal, 22)
                .padding(.vertical, 9)
                .background(HColor.paperDeep.opacity(0.6))
            }
            if context.candidates.isEmpty {
                EmptyPanel(
                    icon: "magnifyingglass",
                    title: "No candidates",
                    message: "Try a shorter title, a different artist spelling, or another provider in Settings."
                )
                .padding(22)
                Spacer()
            } else {
                ScrollView {
                    LazyVStack(spacing: 0) {
                        ForEach(context.candidates) { candidate in
                            CandidateRow(candidate: candidate, appliedMessage: context.appliedMessage)
                            Hairline()
                        }
                    }
                    .padding(.horizontal, 22)
                }
            }
            if let applied = context.appliedMessage {
                HStack(spacing: 8) {
                    Image(systemName: "checkmark.circle.fill")
                        .foregroundStyle(HColor.moss)
                    Text(applied)
                        .font(HFont.body(11, weight: .medium))
                        .foregroundStyle(HColor.inkSoft)
                    Spacer()
                    Button("Done") { dismiss() }
                        .buttonStyle(PrimaryButtonStyle())
                }
                .padding(.horizontal, 22)
                .padding(.vertical, 13)
                .background(HColor.paperLight)
                .overlay(alignment: .top) { Hairline() }
            }
        }
    }
}

private struct CandidateRow: View {
    @EnvironmentObject private var model: AppModel
    let candidate: MetadataCandidate
    let appliedMessage: String?

    var body: some View {
        HStack(alignment: .top, spacing: 14) {
            CandidateArtwork(urlString: candidate.artworkURLString, title: candidate.title)
            VStack(alignment: .leading, spacing: 6) {
                HStack(spacing: 7) {
                    Tag(text: candidate.source.title, color: HColor.blue)
                    Text("\(Int(candidate.score * 100))% match")
                        .font(HFont.mono(9))
                        .foregroundStyle(HColor.muted)
                }
                Text(candidate.title)
                    .font(HFont.body(13, weight: .medium))
                    .foregroundStyle(HColor.ink)
                Text(candidate.subtitle)
                    .font(HFont.body(11))
                    .foregroundStyle(HColor.muted)
                HStack(spacing: 8) {
                    if let year = candidate.year, !year.isEmpty { Tag(text: year, color: HColor.muted) }
                    if let genre = candidate.genre, !genre.isEmpty { Tag(text: genre, color: HColor.muted) }
                    if let detail = candidate.detail, !detail.isEmpty {
                        Text(detail)
                            .font(HFont.body(10))
                            .foregroundStyle(HColor.faint)
                            .lineLimit(1)
                    }
                }
                if !candidate.patch.isEmpty {
                    Text(candidate.patch.keys.sorted { $0.rawValue < $1.rawValue }.map { $0.title }.joined(separator: " · "))
                        .font(HFont.body(9.5))
                        .foregroundStyle(HColor.faint)
                        .lineLimit(1)
                }
            }
            Spacer()
            Button("Apply") {
                Task { await model.applyMetadata(candidate) }
            }
            .buttonStyle(SecondaryButtonStyle())
        }
        .padding(.vertical, 13)
    }
}

private struct CandidateArtwork: View {
    let urlString: String?
    let title: String

    var body: some View {
        Group {
            if let urlString, let url = URL(string: urlString) {
                AsyncImage(url: url) { phase in
                    switch phase {
                    case .success(let image):
                        image.resizable().aspectRatio(contentMode: .fill)
                    default:
                        CoverArt(title: title, artist: "", size: 58, corner: 2)
                    }
                }
            } else {
                CoverArt(title: title, artist: "", size: 58, corner: 2)
            }
        }
        .frame(width: 58, height: 58)
        .clipShape(RoundedRectangle(cornerRadius: 2, style: .continuous))
        .overlay(
            RoundedRectangle(cornerRadius: 2, style: .continuous)
                .stroke(HColor.rule, lineWidth: 1)
        )
    }
}
