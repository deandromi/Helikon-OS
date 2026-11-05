import AppKit
import CompanionCore
import SwiftUI

struct AudiobooksView: View {
    @EnvironmentObject private var model: AppModel
    @State private var searchTerm = ""
    @State private var selectedID: String?

    private var selected: Audiobook? {
        guard let selectedID else { return model.snapshot.audiobooks.first }
        return model.snapshot.audiobooks.first(where: { $0.id == selectedID }) ?? model.snapshot.audiobooks.first
    }

    var body: some View {
        VStack(spacing: 0) {
            header
            searchStrip
            Hairline()
            if model.snapshot.audiobooks.isEmpty && model.audiobookSearchResults.isEmpty {
                ScrollView {
                    EmptyPanel(
                        icon: "book.closed",
                        title: "The audiobook shelf is empty",
                        message: "Search Open Library for catalogue entries and LibriVox for public-domain recordings. Downloads stay on the Mac until you transfer them.",
                        actionTitle: "Search audiobooks",
                        action: { model.section = .audiobooks }
                    )
                    .padding(32)
                }
            } else {
                HStack(spacing: 0) {
                    shelf
                        .frame(width: 296)
                    VerticalHairline()
                    detail
                }
            }
        }
        .background(HColor.paper)
    }

    private var header: some View {
        HStack(alignment: .top, spacing: 20) {
            SectionTitle(
                "Audiobooks",
                kicker: "The reading shelf",
                subtitle: "Cataloguing and public-domain recordings in one shelf. The companion downloads a reading, then transfers the finished files without touching the device catalogue."
            )
            Spacer(minLength: 20)
        }
        .padding(.horizontal, 32)
        .padding(.top, 30)
        .padding(.bottom, 18)
    }

    private var searchStrip: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack(spacing: 10) {
                SearchField(placeholder: "Title, author, subject…", text: $searchTerm)
                    .frame(maxWidth: 360)
                Button {
                    Task { await model.searchAudiobooks(term: searchTerm) }
                } label: {
                    Label("Search books", systemImage: "magnifyingglass")
                }
                .buttonStyle(SecondaryButtonStyle())
                .disabled(searchTerm.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty || model.isSearchingAudiobooks)
                if model.isSearchingAudiobooks {
                    ProgressView().controlSize(.small)
                }
                Spacer()
                Text("Open Library · LibriVox")
                    .font(HFont.mono(9))
                    .foregroundStyle(HColor.faint)
            }
            if !model.audiobookSearchResults.isEmpty {
                ScrollView(.horizontal, showsIndicators: false) {
                    HStack(spacing: 12) {
                        ForEach(model.audiobookSearchResults) { book in
                            AudiobookSearchCard(book: book)
                        }
                    }
                    .padding(.vertical, 2)
                }
            }
        }
        .padding(.horizontal, 32)
        .padding(.bottom, 16)
    }

    private func fileSize(_ path: String) -> Int64 {
        guard let attributes = try? FileManager.default.attributesOfItem(atPath: path),
              let size = attributes[.size] as? NSNumber else { return 0 }
        return size.int64Value
    }

    private var shelf: some View {
        ScrollView {
            LazyVStack(spacing: 0) {
                ForEach(model.snapshot.audiobooks) { book in
                    Button {
                        selectedID = book.id
                    } label: {
                        HStack(alignment: .top, spacing: 10) {
                            AudiobookCover(book: book, size: 48)
                            VStack(alignment: .leading, spacing: 3) {
                                Text(book.title)
                                    .font(HFont.body(11.5, weight: .medium))
                                    .foregroundStyle(HColor.ink)
                                    .lineLimit(2)
                                Text(book.author)
                                    .font(HFont.body(10))
                                    .foregroundStyle(HColor.muted)
                                    .lineLimit(1)
                                Text(book.fileCount > 0 ? "\(book.fileCount) files on Mac" : "Not downloaded")
                                    .font(HFont.body(9.5))
                                    .foregroundStyle(book.fileCount > 0 ? HColor.moss : HColor.faint)
                            }
                            Spacer(minLength: 2)
                            if book.isPublicDomain { Tag(text: "PD", color: HColor.moss) }
                        }
                        .padding(.horizontal, 16)
                        .padding(.vertical, 11)
                        .background(selected?.id == book.id ? HColor.paperDeep.opacity(0.65) : Color.clear)
                        .contentShape(Rectangle())
                    }
                    .buttonStyle(.plain)
                    Hairline()
                }
            }
        }
    }

    @ViewBuilder
    private var detail: some View {
        if let book = selected {
            ScrollView {
                VStack(alignment: .leading, spacing: 18) {
                    HStack(alignment: .top, spacing: 18) {
                        AudiobookCover(book: book, size: 148)
                        VStack(alignment: .leading, spacing: 6) {
                            HStack(spacing: 7) {
                                Tag(text: book.source.title, color: HColor.ochre)
                                if book.isPublicDomain { Tag(text: "Public domain", color: HColor.moss) }
                                if book.isDemo { Tag(text: "Sample", color: HColor.ochre) }
                            }
                            Text(book.title)
                                .font(HFont.display(26))
                                .foregroundStyle(HColor.ink)
                                .fixedSize(horizontal: false, vertical: true)
                            Text(book.subtitle)
                                .font(HFont.body(11.5))
                                .foregroundStyle(HColor.inkSoft)
                            if let duration = book.duration {
                                Text("\(Formatters.duration(duration)) · \(book.fileCount) local files")
                                    .font(HFont.mono(9.5))
                                    .foregroundStyle(HColor.muted)
                            }
                            HStack(spacing: 8) {
                                if book.localPaths.isEmpty && !book.remoteFileURLs.isEmpty {
                                    Button {
                                        Task { await model.downloadAudiobook(book) }
                                    } label: {
                                        Label("Download \(book.remoteFileURLs.count) files", systemImage: "arrow.down.circle")
                                    }
                                    .buttonStyle(PrimaryButtonStyle())
                                } else if !book.localPaths.isEmpty {
                                    Button {
                                        Task { await model.transferAudiobook(book) }
                                    } label: {
                                        Label("Transfer to iPod", systemImage: "arrow.up.arrow.down")
                                    }
                                    .buttonStyle(PrimaryButtonStyle())
                                    Button {
                                        NSWorkspace.shared.activateFileViewerSelecting(book.localPaths.map { URL(fileURLWithPath: $0) })
                                    } label: {
                                        Image(systemName: "folder")
                                            .font(.system(size: 11))
                                            .foregroundStyle(HColor.inkSoft)
                                            .frame(width: 28, height: 28)
                                            .overlay(Circle().stroke(HColor.rule, lineWidth: 1))
                                    }
                                    .buttonStyle(.plain)
                                }
                                if let source = book.sourceURLString, let url = URL(string: source) {
                                    Button("Source") {
                                        NSWorkspace.shared.open(url)
                                    }
                                    .buttonStyle(QuietButtonStyle())
                                }
                            }
                            .padding(.top, 2)
                        }
                    }
                    if !book.summary.isEmpty {
                        Text(book.summary)
                            .font(HFont.body(11.5))
                            .foregroundStyle(HColor.muted)
                            .lineSpacing(3)
                    }
                    if !book.localPaths.isEmpty {
                        VStack(alignment: .leading, spacing: 7) {
                            EyebrowLabel(text: "Local files")
                            ForEach(book.localPaths, id: \.self) { path in
                                HStack(spacing: 8) {
                                    Image(systemName: "waveform")
                                        .font(.system(size: 9))
                                        .foregroundStyle(HColor.faint)
                                    Text(URL(fileURLWithPath: path).lastPathComponent)
                                        .font(HFont.body(10.5))
                                        .foregroundStyle(HColor.inkSoft)
                                        .lineLimit(1)
                                    Spacer()
                                    Text(Formatters.bytes(fileSize(path)))
                                        .font(HFont.mono(9))
                                        .foregroundStyle(HColor.muted)
                                }
                                .padding(.vertical, 3)
                            }
                        }
                    }
                    Hairline()
                    Text("The book is a catalogue record until a reading is downloaded. A transfer copies the local audio files to the selected audiobook folder; it does not create a device database entry yet.")
                        .font(HFont.body(10))
                        .foregroundStyle(HColor.faint)
                        .lineSpacing(2)
                }
                .padding(24)
            }
        } else {
            EmptyPanel(
                icon: "book.closed",
                title: "Choose a book",
                message: "Saved audiobooks and their local files appear here."
            )
            .padding(32)
        }
    }
}

private struct AudiobookSearchCard: View {
    @EnvironmentObject private var model: AppModel
    let book: Audiobook

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            AudiobookCover(book: book, size: 128)
            HStack(spacing: 5) {
                Tag(text: book.source.shortTitle, color: HColor.ochre)
                if book.isPublicDomain { Tag(text: "PD", color: HColor.moss) }
            }
            Text(book.title)
                .font(HFont.body(11.5, weight: .medium))
                .foregroundStyle(HColor.ink)
                .lineLimit(2)
            Text(book.author)
                .font(HFont.body(10))
                .foregroundStyle(HColor.muted)
                .lineLimit(1)
            HStack {
                if book.remoteFileURLs.isEmpty {
                    Text("Catalogue")
                        .font(HFont.mono(9))
                        .foregroundStyle(HColor.faint)
                } else {
                    Text("\(book.remoteFileURLs.count) files")
                        .font(HFont.mono(9))
                        .foregroundStyle(HColor.faint)
                }
                Spacer()
                Button(book.localPaths.isEmpty ? "Save" : "Saved") {
                    model.addAudiobook(book)
                }
                .buttonStyle(SecondaryButtonStyle())
                .disabled(!book.localPaths.isEmpty)
            }
        }
        .padding(10)
        .frame(width: 148)
        .background(HColor.paperLight)
        .overlay(
            RoundedRectangle(cornerRadius: 8, style: .continuous)
                .stroke(HColor.rule, lineWidth: 1)
        )
        .clipShape(RoundedRectangle(cornerRadius: 8, style: .continuous))
    }
}

private struct AudiobookCover: View {
    let book: Audiobook
    let size: CGFloat

    var body: some View {
        Group {
            if let path = book.coverPath, let image = NSImage(contentsOfFile: path) {
                Image(nsImage: image).resizable().aspectRatio(contentMode: .fill)
            } else if let value = book.coverURLString, let url = URL(string: value) {
                AsyncImage(url: url) { phase in
                    switch phase {
                    case .success(let image):
                        image.resizable().aspectRatio(contentMode: .fill)
                    default:
                        CoverArt(title: book.title, artist: book.author, size: size, corner: 3)
                    }
                }
            } else {
                CoverArt(title: book.title, artist: book.author, size: size, corner: 3)
            }
        }
        .frame(width: size, height: size)
        .clipShape(RoundedRectangle(cornerRadius: 3, style: .continuous))
        .overlay(
            RoundedRectangle(cornerRadius: 3, style: .continuous)
                .stroke(HColor.rule, lineWidth: 1)
        )
    }
}
