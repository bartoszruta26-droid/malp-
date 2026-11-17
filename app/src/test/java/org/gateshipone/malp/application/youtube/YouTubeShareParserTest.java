package org.gateshipone.malp.application.youtube;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;

import java.util.List;

import static org.junit.Assert.*;

@RunWith(RobolectricTestRunner.class)
public class YouTubeShareParserTest {
    @Test public void extractsWatchUrlFromSharedText() {
        List<String> urls = YouTubeShareParser.extractYouTubeUrls(
                "Check this out https://www.youtube.com/watch?v=dQw4w9WgXcQ&t=12");
        assertEquals(1, urls.size());
        assertTrue(urls.get(0).contains("watch?v=dQw4w9WgXcQ"));
    }

    @Test public void acceptsShortUrl() {
        assertTrue(YouTubeShareParser.isYouTubeUrl("https://youtu.be/dQw4w9WgXcQ"));
    }

    @Test public void acceptsPlaylist() {
        assertTrue(YouTubeShareParser.isYouTubeUrl(
                "https://www.youtube.com/playlist?list=PL123456789012345"));
    }

    @Test public void rejectsLookalikeHost() {
        assertFalse(YouTubeShareParser.isYouTubeUrl(
                "https://youtube.com.evil.example/watch?v=dQw4w9WgXcQ"));
    }

    @Test public void rejectsHttpAndArbitraryHosts() {
        assertFalse(YouTubeShareParser.isYouTubeUrl(
                "http://youtube.com/watch?v=dQw4w9WgXcQ"));
        assertFalse(YouTubeShareParser.isYouTubeUrl(
                "https://example.com/watch?v=dQw4w9WgXcQ"));
    }

    @Test public void removesTrailingPunctuation() {
        List<String> urls = YouTubeShareParser.extractYouTubeUrls(
                "https://youtu.be/dQw4w9WgXcQ.");
        assertEquals(1, urls.size());
        assertEquals("https://youtu.be/dQw4w9WgXcQ", urls.get(0));
    }

    @Test public void rejectsWatchWithoutValidVideoId() {
        assertFalse(YouTubeShareParser.isYouTubeUrl("https://www.youtube.com/watch"));
        assertFalse(YouTubeShareParser.isYouTubeUrl(
                "https://www.youtube.com/watch?v=abc"));
    }

    @Test public void rejectsPlaylistWithoutValidListId() {
        assertFalse(YouTubeShareParser.isYouTubeUrl("https://www.youtube.com/playlist"));
        assertFalse(YouTubeShareParser.isYouTubeUrl(
                "https://www.youtube.com/playlist?list=short"));
    }

    @Test public void acceptsShortsAndLiveWithValidVideoId() {
        assertTrue(YouTubeShareParser.isYouTubeUrl(
                "https://www.youtube.com/shorts/dQw4w9WgXcQ"));
        assertTrue(YouTubeShareParser.isYouTubeUrl(
                "https://www.youtube.com/live/dQw4w9WgXcQ"));
    }

    @Test public void rejectsUnexpectedSubdomainAndPathSuffix() {
        assertFalse(YouTubeShareParser.isYouTubeUrl(
                "https://www.youtu.be/dQw4w9WgXcQ"));
        assertFalse(YouTubeShareParser.isYouTubeUrl(
                "https://www.youtube.com/shorts/dQw4w9WgXcQ/extra"));
    }
    @Test public void extractsMultipleDistinctUrlsInEncounterOrder() {
        List<String> urls = YouTubeShareParser.extractYouTubeUrls(
                "First https://youtu.be/dQw4w9WgXcQ then "
                        + "https://www.youtube.com/watch?v=abcdefghijk and "
                        + "https://youtu.be/dQw4w9WgXcQ");
        assertEquals(2, urls.size());
        assertEquals("https://youtu.be/dQw4w9WgXcQ", urls.get(0));
        assertTrue(urls.get(1).contains("watch?v=abcdefghijk"));
    }

    @Test public void extractsLinksFromMultipleTextPayloadsAndDeduplicates() {
        List<String> urls = YouTubeShareParser.extractYouTubeUrls(
                java.util.Arrays.asList(
                        "https://youtu.be/dQw4w9WgXcQ",
                        "shared text without a link",
                        "https://www.youtube.com/watch?v=abcdefghijk",
                        "https://youtu.be/dQw4w9WgXcQ"));
        assertEquals(2, urls.size());
        assertEquals("https://youtu.be/dQw4w9WgXcQ", urls.get(0));
        assertTrue(urls.get(1).contains("watch?v=abcdefghijk"));
    }

    @Test public void ignoresNonTextPayloadsInMultipleShare() {
        List<String> urls = YouTubeShareParser.extractYouTubeUrls(
                java.util.Arrays.asList(
                        null,
                        Integer.valueOf(42),
                        "https://youtu.be/dQw4w9WgXcQ"));
        assertEquals(1, urls.size());
        assertEquals("https://youtu.be/dQw4w9WgXcQ", urls.get(0));
    }

    @Test public void nullMultipleSharePayloadReturnsNoUrls() {
        assertTrue(YouTubeShareParser.extractYouTubeUrls((Iterable<?>) null).isEmpty());
    }

    @Test public void ignoresInvalidLinksAmongValidSharedLinks() {
        List<String> urls = YouTubeShareParser.extractYouTubeUrls(
                "https://example.com/video https://youtu.be/dQw4w9WgXcQ");
        assertEquals(1, urls.size());
        assertEquals("https://youtu.be/dQw4w9WgXcQ", urls.get(0));
    }

    @Test public void emptyAndNullShareTextReturnNoUrls() {
        assertTrue(YouTubeShareParser.extractYouTubeUrls(null).isEmpty());
        assertTrue(YouTubeShareParser.extractYouTubeUrls("").isEmpty());
    }

    @Test public void rejectsUserInfoAndNonStandardPorts() {
        assertFalse(YouTubeShareParser.isYouTubeUrl(
                "https://user@youtube.com/watch?v=dQw4w9WgXcQ"));
        assertFalse(YouTubeShareParser.isYouTubeUrl(
                "https://youtube.com:8443/watch?v=dQw4w9WgXcQ"));
    }

}
