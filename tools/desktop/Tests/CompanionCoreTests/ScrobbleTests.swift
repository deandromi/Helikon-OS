import XCTest
@testable import CompanionCore

final class ScrobbleTests: XCTestCase {
    func testLastFMSignatureUsesAlphabeticalUnsignedParameters() async {
        let client = LastFMClient()
        let signature = await client.signature(
            params: [
                "method": "auth.getToken",
                "api_key": "key",
                "format": "json"
            ],
            secret: "secret"
        )
        // md5("api_keykeymethodauth.getTokensecret")
        XCTAssertEqual(signature, "b4705499705a550b07ca058a15bde9b0")
    }

    func testRockboxLogImport() {
        let text = """
        # .scrobbler.log
        Aster Vale\tThe Cartographer\tInk and Latitude\t284\t0\t1700000000
        Marin House\tLow Tide\tSalt Room\t218\t0\t1700000100
        """
        let entries = ScrobbleImporter.parseRockboxLog(Data(text.utf8))
        XCTAssertEqual(entries.count, 2)
        XCTAssertEqual(entries[0].artist, "Aster Vale")
        XCTAssertEqual(entries[0].title, "Ink and Latitude")
        XCTAssertEqual(entries[0].album, "The Cartographer")
        XCTAssertEqual(entries[0].duration, 284)
        XCTAssertEqual(entries[0].status, .queued)
        XCTAssertEqual(entries[1].timestamp.timeIntervalSince1970, 1_700_000_100, accuracy: 1)
    }

    func testJSONPlayHistoryImport() {
        let json = """
        [
          {"artist":"Aster Vale","title":"Fold the Coast","album":"The Cartographer","timestamp":1700000200,"duration":295},
          {"artist":"Marin House","title":"Low Tide","playedAt":"2023-11-14T22:13:20Z"}
        ]
        """
        let entries = ScrobbleImporter.parseJSON(Data(json.utf8))
        XCTAssertEqual(entries.count, 2)
        XCTAssertEqual(entries[0].source, .playHistory)
        XCTAssertEqual(entries[0].title, "Fold the Coast")
        XCTAssertEqual(entries[1].artist, "Marin House")
    }

    func testEmptyAndMalformedLogsAreIgnored() {
        XCTAssertTrue(ScrobbleImporter.parseRockboxLog(Data("not a log\n\n".utf8)).isEmpty)
        XCTAssertTrue(ScrobbleImporter.parseJSON(Data("{}".utf8)).isEmpty)
    }
}
