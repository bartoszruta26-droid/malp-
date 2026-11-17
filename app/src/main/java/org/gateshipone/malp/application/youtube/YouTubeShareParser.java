/*
 *  Copyright (C) 2026 MALP contributors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 */
package org.gateshipone.malp.application.youtube;

import android.net.Uri;
import android.text.TextUtils;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Locale;
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
        if (TextUtils.isEmpty(sharedText)) {
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

    public static boolean isYouTubeUrl(String value) {
        return validateAndNormalize(value) != null;
    }

    private static String validateAndNormalize(String candidate) {
        if (TextUtils.isEmpty(candidate)) {
            return null;
        }
        try {
            Uri uri = Uri.parse(candidate);
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
                validPath = isVideoId(uri.getQueryParameter("v"));
            } else if ("/playlist".equals(path)) {
                validPath = isPlaylistId(uri.getQueryParameter("list"));
            } else {
                List<String> segments = uri.getPathSegments();
                validPath = segments.size() == 2
                        && ("shorts".equals(segments.get(0))
                            || "live".equals(segments.get(0))
                            || "embed".equals(segments.get(0)))
                        && isVideoId(segments.get(1));
            }
            if (!validPath) {
                return null;
            }
            return uri.toString();
        } catch (RuntimeException ignored) {
            return null;
        }
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
