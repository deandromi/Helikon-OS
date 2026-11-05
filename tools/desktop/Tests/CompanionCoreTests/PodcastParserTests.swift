import XCTest
@testable import CompanionCore

final class PodcastParserTests: XCTestCase {
    func testRSSFeedParsesShowAndEpisodes() throws {
        let xml = """
        <?xml version="1.0" encoding="UTF-8"?>
        <rss version="2.0" xmlns:itunes="http://www.itunes.com/dtds/podcast-1.0.dtd">
          <channel>
            <title>The Long Way Round</title>
            <description>A sample show about slow travel.</description>
            <itunes:author>Sample Audio</itunes:author>
            <itunes:image href="https://example.com/cover.jpg"/>
            <item>
              <title>A Road With No Name</title>
              <description>&lt;p&gt;A conversation about the route.&lt;/p&gt;</description>
              <pubDate>Tue, 04 Nov 2025 10:00:00 +0000</pubDate>
              <itunes:duration>47:20</itunes:duration>
              <guid>episode-1</guid>
              <enclosure url="https://example.com/one.mp3" type="audio/mpeg" length="1234"/>
            </item>
            <item>
              <title>The Cartographer's Desk</title>
              <description>Maps and memory.</description>
              <pubDate>Tue, 28 Oct 2025 10:00:00 +0000</pubDate>
              <itunes:duration>41:55</itunes:duration>
              <guid>episode-2</guid>
              <enclosure url="https://example.com/two.mp3" type="audio/mpeg" length="1234"/>
            </item>
          </channel>
        </rss>
        """
        let url = URL(string: "https://example.com/feed.xml")!
        let show = try PodcastFeedParser.parse(data: Data(xml.utf8), feedURL: url, id: UUID())
        XCTAssertEqual(show.title, "The Long Way Round")
        XCTAssertEqual(show.author, "Sample Audio")
        XCTAssertEqual(show.summary, "A sample show about slow travel.")
        XCTAssertEqual(show.artworkURLString, "https://example.com/cover.jpg")
        XCTAssertEqual(show.episodes.count, 2)
        XCTAssertEqual(show.episodes[0].title, "A Road With No Name")
        XCTAssertEqual(show.episodes[0].enclosureURL?.absoluteString, "https://example.com/one.mp3")
        XCTAssertEqual(show.episodes[0].duration ?? 0, 2840, accuracy: 0.1)
        XCTAssertEqual(show.episodes[0].summary, "A conversation about the route.")
        XCTAssertEqual(show.episodes[1].duration ?? 0, 2515, accuracy: 0.1)
    }

    func testMalformedFeedDoesNotProduceAShow() {
        let url = URL(string: "https://example.com/feed.xml")!
        XCTAssertThrowsError(try PodcastFeedParser.parse(data: Data("not xml".utf8), feedURL: url, id: UUID()))
    }
}
