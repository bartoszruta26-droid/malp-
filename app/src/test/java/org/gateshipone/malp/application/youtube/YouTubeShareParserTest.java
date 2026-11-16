package org.gateshipone.malp.application.youtube;

import org.junit.Test;

import java.util.List;

import static org.junit.Assert.*;

public class YouTubeShareParserTest {
    @Test public void extractsWatchUrlFromSharedText() {
        List<String> urls = YouTubeShareParser.extractYouTubeUrls(
                "Check this out https://www.youtube.com/watch?v=abc123&t=12");
        assertEquals(1, urls.size());
        assertTrue(urls.get(0).contains("watch?v=abc123"));
    }

    @Test public void acceptsShortUrl() {
        assertTrue(YouTubeShareParser.isYouTubeUrl("https://youtu.be/abc123"));
    }

    @Test public void acceptsPlaylist() {
        assertTrue(YouTubeShareParser.isYouTubeUrl(
                "https://www.youtube.com/playlist?list=PL123"));
    }

    @Test public void rejectsLookalikeHost() {
        assertFalse(YouTubeShareParser.isYouTubeUrl(
                "https://youtube.com.evil.example/watch?v=abc"));
    }

    @Test public void rejectsHttpAndArbitraryHosts() {
        assertFalse(YouTubeShareParser.isYouTubeUrl("http://youtube.com/watch?v=abc"));
        assertFalse(YouTubeShareParser.isYouTubeUrl("https://example.com/watch?v=abc"));
    }

    @Test public void removesTrailingPunctuation() {
        List<String> urls = YouTubeShareParser.extractYouTubeUrls(
                "https://youtu.be/abc123.");
        assertEquals(1, urls.size());
        assertEquals("https://youtu.be/abc123", urls.get(0));
    }
}
