/*
 * Copyright (C) 2026 MALP contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
package org.gateshipone.malp.application.youtube;

import android.content.Context;
import android.content.SharedPreferences;

import androidx.preference.PreferenceManager;

import org.gateshipone.malp.R;

/** Private app preferences for the independent Raspberry Pi YouTube bridge. */
public final class YouTubeBridgePreferences {
    private YouTubeBridgePreferences() { }

    public static String getBaseUrl(Context context) {
        return PreferenceManager.getDefaultSharedPreferences(context)
                .getString(context.getString(R.string.pref_youtube_bridge_url_key), "");
    }

    public static String getToken(Context context) {
        return PreferenceManager.getDefaultSharedPreferences(context)
                .getString(context.getString(R.string.pref_youtube_bridge_token_key), "");
    }

    public static boolean isConfigured(Context context) {
        String url = getBaseUrl(context);
        return url != null && (url.startsWith("http://") || url.startsWith("https://"));
    }

    public static void save(Context context, String baseUrl, String token) {
        SharedPreferences.Editor editor = PreferenceManager.getDefaultSharedPreferences(context).edit();
        editor.putString(context.getString(R.string.pref_youtube_bridge_url_key), baseUrl.trim());
        editor.putString(context.getString(R.string.pref_youtube_bridge_token_key), token.trim());
        editor.apply();
    }
}
