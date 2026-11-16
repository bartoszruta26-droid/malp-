/*
 * Copyright (C) 2026 MALP contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
package org.gateshipone.malp.application.youtube;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/**
 * Small synchronous HTTP client. Call from a worker thread only.
 * The bridge returns stable local MPD-readable stream URLs, never upstream CDN URLs.
 */
public final class YouTubeAudioBridgeClient {
    private static final int CONNECT_TIMEOUT_MS = 10000;
    private static final int READ_TIMEOUT_MS = 30000;
    private static final int MAX_RESPONSE_BYTES = 1024 * 1024;
    private static final int MAX_ITEMS = 200;

    public static final class Item {
        public final String id;
        public final String title;
        public final String artist;
        public final int durationSeconds;
        public final String streamUrl;

        Item(String id, String title, String artist, int durationSeconds, String streamUrl) {
            this.id = id;
            this.title = title;
            this.artist = artist;
            this.durationSeconds = durationSeconds;
            this.streamUrl = streamUrl;
        }
    }

    public static final class Result {
        public final String title;
        public final boolean playlist;
        public final List<Item> items;

        Result(String title, boolean playlist, List<Item> items) {
            this.title = title;
            this.playlist = playlist;
            this.items = Collections.unmodifiableList(items);
        }
    }

    private final String baseUrl;
    private final String token;

    public YouTubeAudioBridgeClient(String baseUrl, String token) {
        if (baseUrl == null || !(baseUrl.startsWith("http://") || baseUrl.startsWith("https://"))) {
            throw new IllegalArgumentException("Bridge URL must use HTTP or HTTPS");
        }
        this.baseUrl = trimTrailingSlash(baseUrl);
        this.token = token == null ? "" : token.trim();
    }

    public Result resolve(String youtubeUrl, String mode) throws IOException, JSONException {
        if (!YouTubeShareParser.isYouTubeUrl(youtubeUrl)) {
            throw new IOException("Unsupported or invalid YouTube URL");
        }
        if (!"enqueue".equals(mode) && !"play_now".equals(mode)) {
            throw new IllegalArgumentException("Unsupported playback mode");
        }

        HttpURLConnection connection = null;
        try {
            URL endpoint = new URL(baseUrl + "/api/v1/resolve");
            connection = (HttpURLConnection) endpoint.openConnection();
            connection.setRequestMethod("POST");
            connection.setConnectTimeout(CONNECT_TIMEOUT_MS);
            connection.setReadTimeout(READ_TIMEOUT_MS);
            connection.setDoOutput(true);
            connection.setRequestProperty("Content-Type", "application/json; charset=utf-8");
            connection.setRequestProperty("Accept", "application/json");
            if (!token.isEmpty()) {
                connection.setRequestProperty("Authorization", "Bearer " + token);
            }

            JSONObject body = new JSONObject();
            body.put("url", youtubeUrl);
            body.put("mode", mode);
            byte[] payload = body.toString().getBytes(StandardCharsets.UTF_8);
            connection.setFixedLengthStreamingMode(payload.length);
            try (OutputStream output = connection.getOutputStream()) {
                output.write(payload);
            }

            int status = connection.getResponseCode();
            InputStream input = status >= 200 && status < 300
                    ? connection.getInputStream() : connection.getErrorStream();
            String response = readBounded(input);
            if (status < 200 || status >= 300) {
                throw new IOException("YouTube bridge HTTP " + status + ": " + response);
            }
            return parseResponse(new JSONObject(response));
        } finally {
            if (connection != null) connection.disconnect();
        }
    }

    private static Result parseResponse(JSONObject json) throws JSONException, IOException {
        if (!json.optBoolean("success", true)) {
            throw new IOException(json.optString("message", "Bridge could not resolve URL"));
        }
        JSONArray entries = json.optJSONArray("items");
        if (entries == null || entries.length() == 0 || entries.length() > MAX_ITEMS) {
            throw new IOException("Bridge returned an empty or oversized item list");
        }

        List<Item> items = new ArrayList<>();
        for (int i = 0; i < entries.length(); i++) {
            JSONObject entry = entries.getJSONObject(i);
            String streamUrl = entry.optString("stream_url", "");
            String scheme = streamUrl.startsWith("http://") ? "http"
                    : streamUrl.startsWith("https://") ? "https" : "";
            if (scheme.isEmpty()) {
                throw new IOException("Bridge returned a non-HTTP stream URL");
            }
            items.add(new Item(entry.optString("id", ""),
                    entry.optString("title", "YouTube item " + (i + 1)),
                    entry.optString("artist", ""), entry.optInt("duration", 0), streamUrl));
        }
        return new Result(json.optString("title", ""), json.optBoolean("playlist", items.size() > 1), items);
    }

    private static String readBounded(InputStream input) throws IOException {
        if (input == null) return "";
        StringBuilder result = new StringBuilder();
        try (BufferedReader reader = new BufferedReader(
                new InputStreamReader(input, StandardCharsets.UTF_8))) {
            char[] buffer = new char[4096];
            int read;
            int total = 0;
            while ((read = reader.read(buffer)) != -1) {
                total += read;
                if (total > MAX_RESPONSE_BYTES) throw new IOException("Bridge response too large");
                result.append(buffer, 0, read);
            }
        }
        return result.toString();
    }

    private static String trimTrailingSlash(String value) {
        int end = value.length();
        while (end > 0 && value.charAt(end - 1) == '/') end--;
        return value.substring(0, end);
    }
}
