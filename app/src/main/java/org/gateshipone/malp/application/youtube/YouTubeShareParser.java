/*
 *  Copyright (C) 2026 MALP contributors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 */
package org.gateshipone.malp.application.youtube;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Locale;
import java.net.URI;
import java.net.URLDecoder;
import java.nio.charset.StandardCharsets;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Strict parser for YouTube links received through Android's share sheet.
 *
 * This class never resolves a remote URL. It only extracts an allow-listed HTTPS
 * YouTube URL so the configured Raspberry Pi bridge can resolve it.
 */
public final class YouTubeShareParser {
    private static final Pattern URL_PATTERN = Pattern.compile(
            "(?i)https?://[^\\s<>\\\"']+");
    private static final Pattern VIDEO_ID_PATTERN = Pattern.compile("[A-Za-z0-9_-]{11}");
    private static final Pattern PLAYLIST_ID_PATTERN = Pattern.compile("[A-Za-z0-9_-]{10,128}");
    private static final List<String> ALLOWED_HOSTS;

    static {
        List<String> hosts = new ArrayList<>();
        hosts.add("youtube.com");
        hosts.add("www.youtube.com");
        hosts.add("m.youtube.com");
        hosts.add("music.youtube.com");
        hosts.add("youtu.be");
        ALLOWED_HOSTS = Collections.unmodifiableList(hosts);
    }

    private YouTubeShareParser() { }

    public static List<String> extractYouTubeUrls(String sharedText) {
        if (sharedText == null || sharedText.isEmpty()) {
            return Collections.emptyList();
        }

        List<String> result = new ArrayList<>();
        Matcher matcher = URL_PATTERN.matcher(sharedText);
        while (matcher.find()) {
            String candidate = trimTrailingPunctuation(matcher.group());
            String normalized = validateAndNormalize(candidate);
            if (normalized != null && !result.contains(normalized)) {
                result.add(normalized);
            }
        }
        return result;
    }

    /** Extract links from the text payloads used by ACTION_SEND_MULTIPLE. */
    public static List<String> extractYouTubeUrlsFromPayloads(Iterable<?> sharedTexts) {
        if (sharedTexts == null) return Collections.emptyList();
        List<String> result = new ArrayList<>();
        for (Object item : sharedTexts) {
            if (!(item instanceof CharSequence)) continue;
            for (String url : extractYouTubeUrls(item.toString())) {
                if (!result.contains(url)) result.add(url);
            }
        }
        return result;
    }

    public static boolean isYouTubeUrl(String value) {
        return validateAndNormalize(value) != null;
    }

    private static String validateAndNormalize(String candidate) {
        if (candidate == null || candidate.isEmpty()) {
            return null;
        }
        try {
            URI uri = new URI(candidate);
            String scheme = uri.getScheme();
            String host = uri.getHost();
            if (!"https".equalsIgnoreCase(scheme) || host == null) {
                return null;
            }
            host = host.toLowerCase(Locale.ROOT);
            if (!ALLOWED_HOSTS.contains(host)) {
                return null;
            }
            if (uri.getUserInfo() != null || uri.getPort() != -1) {
                return null;
            }

            String path = uri.getPath() == null ? "" : uri.getPath();
            boolean validPath;
            if ("youtu.be".equals(host)) {
                validPath = isVideoId(path.startsWith("/") ? path.substring(1) : path);
            } else if ("/watch".equals(path)) {
                validPath = isVideoId(queryParameter(uri.getRawQuery(), "v"));
            } else if ("/playlist".equals(path)) {
                validPath = isPlaylistId(queryParameter(uri.getRawQuery(), "list"));
            } else {
                String[] segments = path.startsWith("/")
                        ? path.substring(1).split("/", -1) : path.split("/", -1);
                validPath = segments.length == 2
                        && ("shorts".equals(segments[0])
                            || "live".equals(segments[0])
                            || "embed".equals(segments[0]))
                        && isVideoId(segments[1]);
            }
            if (!validPath) {
                return null;
            }
            return uri.toString();
        } catch (RuntimeException ignored) {
            return null;
        }
    }

    private static String queryParameter(String rawQuery, String key) {
        if (rawQuery == null) return null;
        for (String pair : rawQuery.split("&")) {
            int separator = pair.indexOf('=');
            String rawName = separator < 0 ? pair : pair.substring(0, separator);
            try {
                String name = URLDecoder.decode(rawName, StandardCharsets.UTF_8.name());
                if (key.equals(name)) {
                    String rawValue = separator < 0 ? "" : pair.substring(separator + 1);
                    return URLDecoder.decode(rawValue, StandardCharsets.UTF_8.name());
                }
            } catch (java.io.UnsupportedEncodingException impossible) {
                // UTF-8 is required by every Java implementation.
                throw new AssertionError(impossible);
            } catch (IllegalArgumentException malformedEncoding) {
                return null;
            }
        }
        return null;
    }

    private static boolean isVideoId(String value) {
        return value != null && VIDEO_ID_PATTERN.matcher(value).matches();
    }

    private static boolean isPlaylistId(String value) {
        return value != null && PLAYLIST_ID_PATTERN.matcher(value).matches();
    }

    private static String trimTrailingPunctuation(String value) {
        int end = value.length();
        while (end > 0) {
            char c = value.charAt(end - 1);
            if (c == '.' || c == ',' || c == ';' || c == ':' || c == '!' || c == '?'
                    || c == ')' || c == ']' || c == '}') {
                end--;
            } else {
                break;
            }
        }
        return value.substring(0, end);
    }
}
