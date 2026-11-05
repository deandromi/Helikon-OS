import AppKit
import CompanionCore
import SwiftUI

struct PodcastsView: View {
    @EnvironmentObject private var model: AppModel
    @State private var searchTerm = ""
    @State private var selectedShowID: UUID?
    @State private var showingAddFeed = false
    @State private var feedURL = ""

    private var selectedShow: PodcastShow? {
        guard let selectedShowID else { return model.snapshot.podcasts.first }
        return model.snapshot.podcasts.first(where: { $0.id == selectedShowID }) ?? model.snapshot.podcasts.first
    }

    var body: some View {
        VStack(spacing: 0) {
            header
            searchStrip
            Hairline()
            if model.snapshot.podcasts.isEmpty && model.podcastSearchResults.isEmpty {
                ScrollView {
                    EmptyPanel(
                        icon: "dot.radiowaves.left.and.right",
                        title: "No podcasts yet",
                        message: "Search the Apple podcast directory or paste a feed URL. The device never fetches a feed; downloads and subscription state live on this Mac.",
                        actionTitle: "Add a feed",
                        action: { showingAddFeed = true }
                    )
                    .padding(32)
                }
            } else {
                HStack(spacing: 0) {
                    showList
                        .frame(width: 292)
                    VerticalHairline()
                    showDetail
                }
            }
        }
        .background(HColor.paper)
        .sheet(isPresented: $showingAddFeed) {
            addFeedSheet
        }
    }

    private var header: some View {
        HStack(alignment: .top, spacing: 20) {
            SectionTitle(
                "Podcasts",
                kicker: "Listening on the Mac",
                subtitle: "Search, subscribe and download on the Mac. The iPod receives finished episodes; it never calls a feed or reports listening."
            )
            Spacer(minLength: 20)
            Button {
                showingAddFeed = true
            } label: {
                Label("Add feed", systemImage: "plus")
            }
            .buttonStyle(PrimaryButtonStyle())
            .padding(.top, 4)
        }
        .padding(.horizontal, 32)
        .padding(.top, 30)
        .padding(.bottom, 18)
    }

    private var searchStrip: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack(spacing: 10) {
                SearchField(placeholder: "Search podcast shows…", text: $searchTerm)
                    .frame(maxWidth: 360)
                Button {
                    Task { await model.searchPodcasts(term: searchTerm) }
                } label: {
                    Label("Search directory", systemImage: "magnifyingglass")
                }
                .buttonStyle(SecondaryButtonStyle())
                .disabled(searchTerm.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty || model.isSearchingPodcasts)
                if model.isSearchingPodcasts {
                    ProgressView().controlSize(.small)
                }
                Spacer()
                if !model.podcastSearchResults.isEmpty {
                    Button("Clear results") {
                        model.podcastSearchResults = []
                    }
                    .buttonStyle(QuietButtonStyle())
                }
            }
            if !model.podcastSearchResults.isEmpty {
                ScrollView(.horizontal, showsIndicators: false) {
                    HStack(spacing: 12) {
                        ForEach(model.podcastSearchResults) { result in
                            PodcastSearchCard(result: result)
                        }
                    }
                    .padding(.vertical, 2)
                }
            }
        }
        .padding(.horizontal, 32)
        .padding(.bottom, 16)
    }

    private var showList: some View {
        ScrollView {
            LazyVStack(spacing: 0) {
                ForEach(model.snapshot.podcasts) { show in
                    Button {
                        selectedShowID = show.id
                    } label: {
                        HStack(alignment: .top, spacing: 10) {
                            CoverArt(
                                title: show.title,
                                artist: show.author,
                                artworkPath: show.artworkPath,
                                size: 44,
                                corner: 3
                            )
                            VStack(alignment: .leading, spacing: 3) {
                                Text(show.title)
                                    .font(HFont.body(11.5, weight: .medium))
                                    .foregroundStyle(HColor.ink)
                                    .lineLimit(2)
                                Text(show.author.isEmpty ? "Podcast" : show.author)
                                    .font(HFont.body(10))
                                    .foregroundStyle(HColor.muted)
                                    .lineLimit(1)
                                Text("\(show.downloadedCount) downloaded · \(show.episodes.count) episodes")
                                    .font(HFont.body(9.5))
                                    .foregroundStyle(HColor.faint)
                            }
                            Spacer(minLength: 2)
                            if show.isDemo {
                                Tag(text: "Sample", color: HColor.ochre)
                            }
                        }
                        .padding(.horizontal, 16)
                        .padding(.vertical, 11)
                        .background(selectedShow?.id == show.id ? HColor.paperDeep.opacity(0.65) : Color.clear)
                        .contentShape(Rectangle())
                    }
                    .buttonStyle(.plain)
                    Hairline()
                }
            }
        }
    }

    @ViewBuilder
    private var showDetail: some View {
        if let show = selectedShow {
            ScrollView {
                VStack(alignment: .leading, spacing: 18) {
                    HStack(alignment: .top, spacing: 18) {
                        CoverArt(
                            title: show.title,
                            artist: show.author,
                            artworkPath: show.artworkPath,
                            size: 132,
                            corner: 4
                        )
                        VStack(alignment: .leading, spacing: 6) {
                            HStack(spacing: 7) {
                                Tag(text: "Podcast", color: HColor.blue)
                                if show.isDemo { Tag(text: "Sample", color: HColor.ochre) }
                            }
                            Text(show.title)
                                .font(HFont.display(25))
                                .foregroundStyle(HColor.ink)
                                .fixedSize(horizontal: false, vertical: true)
                            Text(show.author)
                                .font(HFont.body(11.5, weight: .medium))
                                .foregroundStyle(HColor.inkSoft)
                            Text("Updated \(Formatters.relativeDate(show.lastRefreshed))")
                                .font(HFont.body(10))
                                .foregroundStyle(HColor.muted)
                            HStack(spacing: 8) {
                                Button {
                                    Task { await model.refreshPodcast(show) }
                                } label: {
                                    Label("Refresh feed", systemImage: "arrow.clockwise")
                                }
                                .buttonStyle(SecondaryButtonStyle())
                                .disabled(show.isDemo)
                                Button(role: .destructive) {
                                    model.removePodcast(show)
                                } label: {
                                    Text("Unsubscribe")
                                }
                                .buttonStyle(.plain)
                                .font(HFont.body(10.5))
                                .foregroundStyle(HColor.error)
                            }
                            .padding(.top, 2)
                        }
                    }
                    if !show.summary.isEmpty {
                        Text(show.summary)
                            .font(HFont.body(11.5))
                            .foregroundStyle(HColor.muted)
                            .lineSpacing(3)
                    }
                    Hairline()
                    HStack {
                        Text("Episodes")
                            .font(HFont.display(21))
                            .foregroundStyle(HColor.ink)
                        Spacer()
                        Text("\(show.episodes.count) total")
                            .font(HFont.mono(9.5))
                            .foregroundStyle(HColor.muted)
                    }
                    LazyVStack(spacing: 0) {
                        ForEach(show.episodes) { episode in
                            EpisodeRow(show: show, episode: episode)
                            Hairline()
                        }
                    }
                }
                .padding(24)
            }
        } else {
            EmptyPanel(
                icon: "dot.radiowaves.left.and.right",
                title: "Choose a show",
                message: "Subscribed shows and their episodes appear here."
            )
            .padding(32)
        }
    }

    private var addFeedSheet: some View {
        VStack(alignment: .leading, spacing: 16) {
            HStack {
                VStack(alignment: .leading, spacing: 4) {
                    EyebrowLabel(text: "Subscribe")
                    Text("Add a feed")
                        .font(HFont.display(25))
                        .foregroundStyle(HColor.ink)
                }
                Spacer()
                Button("Cancel") { showingAddFeed = false }
                    .buttonStyle(QuietButtonStyle())
            }
            FilledTagField(label: "RSS or Atom URL", text: $feedURL, placeholder: "https://example.com/feed.xml")
            Text("The feed is fetched once by this Mac. Downloads only start when you choose an episode.")
                .font(HFont.body(10.5))
                .foregroundStyle(HColor.muted)
            HStack {
                Spacer()
                Button("Add feed") {
                    model.addPodcastFeed(feedURL)
                    showingAddFeed = false
                    feedURL = ""
                }
                .buttonStyle(PrimaryButtonStyle())
                .disabled(feedURL.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
            }
        }
        .padding(24)
        .frame(width: 520)
        .background(HColor.paper)
    }
}

private struct PodcastSearchCard: View {
    @EnvironmentObject private var model: AppModel
    let result: PodcastSearchResult

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            CoverArt(title: result.title, artist: result.author, size: 128, corner: 3)
            Text(result.title)
                .font(HFont.body(11.5, weight: .medium))
                .foregroundStyle(HColor.ink)
                .lineLimit(2)
            Text(result.author)
                .font(HFont.body(10))
                .foregroundStyle(HColor.muted)
                .lineLimit(1)
            HStack {
                if let count = result.episodeCount {
                    Text("\(count) eps")
                        .font(HFont.mono(9))
                        .foregroundStyle(HColor.faint)
                }
                Spacer()
                Button("Subscribe") {
                    Task { await model.subscribePodcast(result) }
                }
                .buttonStyle(SecondaryButtonStyle())
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

private struct EpisodeRow: View {
    @EnvironmentObject private var model: AppModel
    let show: PodcastShow
    let episode: PodcastEpisode

    var body: some View {
        HStack(alignment: .top, spacing: 12) {
            VStack(alignment: .leading, spacing: 4) {
                HStack(spacing: 7) {
                    Text(episode.title)
                        .font(HFont.body(11.5, weight: .medium))
                        .foregroundStyle(HColor.ink)
                    if episode.downloadState == .downloaded {
                        Tag(text: "On Mac", color: HColor.moss)
                    }
                    if let error = episode.error {
                        Text(error)
                            .font(HFont.body(9.5))
                            .foregroundStyle(HColor.error)
                            .lineLimit(1)
                    }
                }
                Text(episode.summary)
                    .font(HFont.body(10))
                    .foregroundStyle(HColor.muted)
                    .lineLimit(2)
                HStack(spacing: 10) {
                    Text(Formatters.shortDate(episode.publishedAt))
                    if let duration = episode.duration { Text(Formatters.duration(duration)) }
                }
                .font(HFont.mono(9))
                .foregroundStyle(HColor.faint)
            }
            Spacer()
            if episode.downloadState == .downloading {
                ProgressView().controlSize(.small)
            } else if episode.downloadState == .downloaded {
                HStack(spacing: 8) {
                    Button("Transfer") {
                        Task { await model.transferEpisode(showID: show.id, episodeID: episode.id) }
                    }
                    .buttonStyle(PrimaryButtonStyle())
                    Button {
                        if let path = episode.localPath {
                            NSWorkspace.shared.activateFileViewerSelecting([URL(fileURLWithPath: path)])
                        }
                    } label: {
                        Image(systemName: "folder")
                            .font(.system(size: 10))
                            .foregroundStyle(HColor.inkSoft)
                    }
                    .buttonStyle(.plain)
                }
            } else {
                Button("Download") {
                    Task { await model.downloadEpisode(showID: show.id, episodeID: episode.id) }
                }
                .buttonStyle(SecondaryButtonStyle())
                .disabled(show.isDemo || episode.enclosureURL == nil)
            }
        }
        .padding(.vertical, 11)
    }
}
