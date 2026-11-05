import Foundation

public final class LibraryRepository {
    public let libraryURL: URL

    public init(libraryURL: URL = AppPaths.libraryURL) {
        self.libraryURL = libraryURL
    }

    public func load() -> LibrarySnapshot {
        JSONStore.load(LibrarySnapshot.self, from: libraryURL, defaultValue: .empty)
    }

    public func save(_ snapshot: LibrarySnapshot) throws {
        var copy = snapshot
        copy.savedAt = Date()
        try JSONStore.save(copy, to: libraryURL)
    }
}

public enum LibraryMerger {
    public static func merge(
        scan: LibraryScanResult,
        into snapshot: LibrarySnapshot,
        root: URL,
        prune: Bool = true
    ) -> LibrarySnapshot {
        var updated = snapshot
        let existing = Dictionary(uniqueKeysWithValues: snapshot.tracks.map { ($0.fileURL.standardizedFileURL.path, $0) })
        let scannedPaths = Set(scan.tracks.map { $0.fileURL.standardizedFileURL.path })

        var merged: [Track] = []
        for track in scan.tracks {
            if var old = existing[track.fileURL.standardizedFileURL.path] {
                old.fileURL = track.fileURL
                old.relativePath = track.relativePath
                old.title = track.title
                old.artist = track.artist
                old.album = track.album
                old.albumArtist = track.albumArtist
                old.trackNumber = track.trackNumber
                old.discNumber = track.discNumber
                old.year = old.year ?? track.year
                old.genre = old.genre ?? track.genre
                old.duration = track.duration
                old.fileSize = track.fileSize
                old.format = track.format
                old.artworkPath = old.artworkPath ?? track.artworkPath
                old.hasEmbeddedArtwork = track.hasEmbeddedArtwork
                old.modifiedAt = track.modifiedAt
                merged.append(old)
            } else {
                merged.append(track)
            }
        }

        let rootPath = root.standardizedFileURL.path
        let preserved = snapshot.tracks.filter { track in
            guard prune else { return !scannedPaths.contains(track.fileURL.standardizedFileURL.path) }
            let path = track.fileURL.standardizedFileURL.path
            let isInsideRoot = path == rootPath || path.hasPrefix(rootPath + "/")
            return !isInsideRoot && !scannedPaths.contains(path)
        }
        updated.tracks = (preserved + merged).sorted {
            if $0.album != $1.album { return $0.album.localizedCaseInsensitiveCompare($1.album) == .orderedAscending }
            if ($0.discNumber ?? 0) != ($1.discNumber ?? 0) { return ($0.discNumber ?? 0) < ($1.discNumber ?? 0) }
            return ($0.trackNumber ?? 0) < ($1.trackNumber ?? 0)
        }
        return updated
    }

    public static func apply(_ candidate: MetadataCandidate, to track: Track) -> Track {
        var updated = track
        for (field, value) in candidate.patch {
            guard !value.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else { continue }
            switch field {
            case .title: updated.title = value
            case .artist: updated.artist = value
            case .album: updated.album = value
            case .albumArtist: updated.albumArtist = value
            case .year: updated.year = value
            case .genre: updated.genre = value
            case .artwork: updated.artworkPath = value
            case .description: updated.notes = value
            case .biography: break
            }
            updated.fetchedFields.insert(field)
        }
        updated.metadataSource = candidate.source.title
        return updated
    }

    public static func apply(_ candidate: MetadataCandidate, albumID: String, to snapshot: LibrarySnapshot) -> LibrarySnapshot {
        var updated = snapshot
        var annotation = updated.albumAnnotations[albumID] ?? AlbumAnnotation()
        if let value = candidate.patch[.description], !value.isEmpty { annotation.description = value }
        if let value = candidate.patch[.year], !value.isEmpty { annotation.year = value }
        if let value = candidate.patch[.genre], !value.isEmpty { annotation.genre = value }
        if let value = candidate.patch[.artwork], !value.isEmpty { annotation.artworkPath = value }
        annotation.metadataSource = candidate.source.title
        updated.albumAnnotations[albumID] = annotation

        for index in updated.tracks.indices where updated.tracks[index].albumKey == albumID {
            if let value = candidate.patch[.album], !value.isEmpty { updated.tracks[index].album = value }
            if let value = candidate.patch[.albumArtist], !value.isEmpty { updated.tracks[index].albumArtist = value }
            if let value = candidate.patch[.year], !value.isEmpty { updated.tracks[index].year = value }
            if let value = candidate.patch[.genre], !value.isEmpty { updated.tracks[index].genre = value }
            if let artwork = candidate.patch[.artwork], !artwork.isEmpty {
                updated.tracks[index].artworkPath = artwork
            }
            updated.tracks[index].metadataSource = candidate.source.title
        }
        return updated
    }

    public static func apply(_ candidate: MetadataCandidate, artistID: String, to snapshot: LibrarySnapshot) -> LibrarySnapshot {
        var updated = snapshot
        var annotation = updated.artistAnnotations[artistID] ?? ArtistAnnotation()
        if let value = candidate.patch[.biography], !value.isEmpty { annotation.biography = value }
        if let value = candidate.patch[.artwork], !value.isEmpty { annotation.imageURL = value }
        annotation.metadataSource = candidate.source.title
        updated.artistAnnotations[artistID] = annotation

        for index in updated.tracks.indices where updated.tracks[index].artistKey == artistID {
            if let value = candidate.patch[.artist], !value.isEmpty { updated.tracks[index].artist = value }
            if let value = candidate.patch[.genre], !value.isEmpty { updated.tracks[index].genre = value }
            updated.tracks[index].metadataSource = candidate.source.title
        }
        return updated
    }

    public static func setRating(_ rating: Int, for trackID: UUID, in snapshot: LibrarySnapshot) -> LibrarySnapshot {
        var updated = snapshot
        if let index = updated.tracks.firstIndex(where: { $0.id == trackID }) {
            updated.tracks[index].rating = min(5, max(0, rating))
        }
        return updated
    }

    public static func setAlbumRating(_ rating: Int, albumID: String, in snapshot: LibrarySnapshot) -> LibrarySnapshot {
        var updated = snapshot
        var annotation = updated.albumAnnotations[albumID] ?? AlbumAnnotation()
        annotation.rating = min(5, max(0, rating))
        updated.albumAnnotations[albumID] = annotation
        return updated
    }

    public static func setAlbumDescription(_ description: String, albumID: String, in snapshot: LibrarySnapshot) -> LibrarySnapshot {
        var updated = snapshot
        var annotation = updated.albumAnnotations[albumID] ?? AlbumAnnotation()
        annotation.description = description
        updated.albumAnnotations[albumID] = annotation
        return updated
    }

    public static func setArtistBiography(_ biography: String, artistID: String, in snapshot: LibrarySnapshot) -> LibrarySnapshot {
        var updated = snapshot
        var annotation = updated.artistAnnotations[artistID] ?? ArtistAnnotation()
        annotation.biography = biography
        updated.artistAnnotations[artistID] = annotation
        return updated
    }

    public static func removeTrack(_ trackID: UUID, from snapshot: LibrarySnapshot) -> LibrarySnapshot {
        var updated = snapshot
        updated.tracks.removeAll { $0.id == trackID }
        return updated
    }
}
