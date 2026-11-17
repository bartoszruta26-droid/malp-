/*
 * Copyright (C) 2026 MALP contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
package org.gateshipone.malp.application.youtube;

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

/** Authenticated client for the Raspberry Pi queue API. Invoke on a worker thread. */
public final class YouTubeAudioBridgeClient {
    private static final int CONNECT_TIMEOUT_MS = 10000;
    private static final int READ_TIMEOUT_MS = 65000;
    private static final int MAX_RESPONSE_BYTES = 256 * 1024;
    private static final int MAX_ITEMS = 200;

    public static final class Result {
        public final String requestId;
        public final String title;
        public final boolean playlist;
        public final int queued;
        public final boolean started;

        Result(String requestId, String title, boolean playlist, int queued, boolean started) {
            this.requestId = requestId;
            this.title = title;
            this.playlist = playlist;
            this.queued = queued;
            this.started = started;
        }
    }

    private final String baseUrl;
    private final String token;

    public YouTubeAudioBridgeClient(String baseUrl, String token) {
        if (baseUrl == null || token == null) {
            throw new IllegalArgumentException("Bridge URL and access token are required");
        }
        String trimmed = baseUrl.trim();
        if (!trimmed.startsWith("https://")
                || trimmed.indexOf('@') >= 0 || trimmed.indexOf('?') >= 0
                || trimmed.indexOf('#') >= 0) {
            throw new IllegalArgumentException("Bridge URL must be an HTTPS origin without credentials or query");
        }
        String trimmedToken = token.trim();
        if (!isValidBearerToken(trimmedToken)) {
            throw new IllegalArgumentException("Bridge token must be 43-128 base64url characters");
        }
        this.baseUrl = trimTrailingSlash(trimmed);
        this.token = trimmedToken;
    }

    public Result queue(String youtubeUrl, String mode) throws IOException, JSONException {
        if (!YouTubeShareParser.isYouTubeUrl(youtubeUrl)) {
            throw new IOException("Unsupported or invalid YouTube URL");
        }
        if (!"enqueue".equals(mode) && !"play_now".equals(mode)) {
            throw new IllegalArgumentException("Unsupported playback mode");
        }

        HttpURLConnection connection = null;
        try {
            URL endpoint = new URL(baseUrl + "/api/v1/queue");
            connection = (HttpURLConnection) endpoint.openConnection();
            connection.setInstanceFollowRedirects(false);
            connection.setRequestMethod("POST");
            connection.setConnectTimeout(CONNECT_TIMEOUT_MS);
            connection.setReadTimeout(READ_TIMEOUT_MS);
            connection.setDoOutput(true);
            connection.setRequestProperty("Content-Type", "application/json; charset=utf-8");
            connection.setRequestProperty("Accept", "application/json");
            connection.setRequestProperty("Authorization", "Bearer " + token);

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
            JSONObject response = new JSONObject(readBounded(input));
            if (status < 200 || status >= 300) {
                JSONObject error = response.optJSONObject("error");
                String message = error == null
                        ? "Bridge HTTP " + status
                        : error.optString("message", "Bridge HTTP " + status);
                if (!response.optBoolean("rollback_complete", true)) {
                    int knownQueued = response.optInt("queued", 0);
                    message += ". Wynik MPD jest niejednoznaczny; co najmniej "
                            + Math.max(0, knownQueued)
                            + " pozycji mogło zostać dodanych. Sprawdź kolejkę przed ponowieniem.";
                }
                throw new IOException(message);
            }
            return parseResponse(response);
        } finally {
            if (connection != null) connection.disconnect();
        }
    }

    private static Result parseResponse(JSONObject json) throws JSONException, IOException {
        if (!json.optBoolean("success", false)) {
            JSONObject error = json.optJSONObject("error");
            throw new IOException(error == null
                    ? "Bridge did not confirm queue insertion"
                    : error.optString("message", "Bridge did not confirm queue insertion"));
        }
        int queued = json.optInt("queued", -1);
        if (queued < 1 || queued > MAX_ITEMS) {
            throw new IOException("Bridge returned an invalid queued item count");
        }
        // API v1 confirms the queue count and playback state. A request identifier
        // is optional and is not fabricated by the server.
        String requestId = json.optString("request_id", "");
        return new Result(requestId, json.optString("title", ""),
                json.optBoolean("playlist", queued > 1), queued,
                json.optBoolean("started", false));
    }

    private static String readBounded(InputStream input) throws IOException {
        if (input == null) throw new IOException("Bridge returned an empty response");
        StringBuilder result = new StringBuilder();
        try (BufferedReader reader = new BufferedReader(
                new InputStreamReader(input, StandardCharsets.UTF_8))) {
            char[] buffer = new char[2048];
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

    private static boolean isValidBearerToken(String value) {
        if (value.length() < 43 || value.length() > 128) return false;
        for (int i = 0; i < value.length(); i++) {
            char c = value.charAt(i);
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                    || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
        }
        return true;
    }

    private static String trimTrailingSlash(String value) {
        int end = value.length();
        while (end > 0 && value.charAt(end - 1) == '/') end--;
        return value.substring(0, end);
    }
}
