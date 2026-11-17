/*
 * Copyright (C) 2026 MALP contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
package org.gateshipone.malp.application.youtube;

import android.content.Context;
import android.content.SharedPreferences;
import android.security.KeyPairGeneratorSpec;
import android.util.Base64;

import androidx.preference.PreferenceManager;

import org.gateshipone.malp.R;

import java.math.BigInteger;
import java.nio.charset.StandardCharsets;
import java.security.KeyPairGenerator;
import java.security.KeyStore;
import java.security.SecureRandom;
import java.security.spec.AlgorithmParameterSpec;
import java.util.Calendar;
import java.util.Date;

import javax.crypto.Cipher;
import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import javax.crypto.spec.GCMParameterSpec;
import javax.security.auth.x500.X500Principal;

/** Bridge settings; the bearer token is encrypted with a key protected by Android Keystore. */
public final class YouTubeBridgePreferences {
    private static final String KEYSTORE_PROVIDER = "AndroidKeyStore";
    private static final String KEY_ALIAS = "malp.youtube.bridge.token.wrap.v1";
    private static final String SECURE_PREFS = "youtube_bridge_secure";
    private static final String WRAPPED_KEY = "wrapped_aes_key";
    private static final String IV = "token_iv";
    private static final String CIPHERTEXT = "token_ciphertext";

    private YouTubeBridgePreferences() { }

    public static String getBaseUrl(Context context) {
        return PreferenceManager.getDefaultSharedPreferences(context)
                .getString(context.getString(R.string.pref_youtube_bridge_url_key), "");
    }

    public static String getToken(Context context) {
        // Remove any token left by the former plaintext preference implementation.
        PreferenceManager.getDefaultSharedPreferences(context).edit()
                .remove(context.getString(R.string.pref_youtube_bridge_token_key)).apply();
        SharedPreferences secure = context.getApplicationContext()
                .getSharedPreferences(SECURE_PREFS, Context.MODE_PRIVATE);
        String wrapped = secure.getString(WRAPPED_KEY, null);
        String ivValue = secure.getString(IV, null);
        String ciphertextValue = secure.getString(CIPHERTEXT, null);
        if (wrapped == null || ivValue == null || ciphertextValue == null) return "";

        try {
            KeyStore keyStore = loadKeyStore();
            if (!keyStore.containsAlias(KEY_ALIAS)) return "";
            byte[] wrappedKey = Base64.decode(wrapped, Base64.NO_WRAP);
            byte[] iv = Base64.decode(ivValue, Base64.NO_WRAP);
            byte[] ciphertext = Base64.decode(ciphertextValue, Base64.NO_WRAP);

            Cipher rsa = Cipher.getInstance("RSA/ECB/PKCS1Padding");
            rsa.init(Cipher.DECRYPT_MODE, keyStore.getKey(KEY_ALIAS, null));
            byte[] rawAesKey = rsa.doFinal(wrappedKey);

            SecretKey aesKey = new javax.crypto.spec.SecretKeySpec(rawAesKey, "AES");
            Cipher aes = Cipher.getInstance("AES/GCM/NoPadding");
            aes.init(Cipher.DECRYPT_MODE, aesKey, new GCMParameterSpec(128, iv));
            return new String(aes.doFinal(ciphertext), StandardCharsets.UTF_8);
        } catch (Exception error) {
            // Fail closed: never fall back to a plaintext preference.
            return "";
        }
    }

    public static boolean isConfigured(Context context) {
        String url = getBaseUrl(context);
        return url != null && (url.startsWith("http://") || url.startsWith("https://"))
                && !getToken(context).isEmpty();
    }

    public static void save(Context context, String baseUrl, String token) {
        if (baseUrl == null || token == null) {
            throw new IllegalArgumentException("Bridge URL and token are required");
        }
        PreferenceManager.getDefaultSharedPreferences(context).edit()
                .putString(context.getString(R.string.pref_youtube_bridge_url_key), baseUrl.trim())
                .remove(context.getString(R.string.pref_youtube_bridge_token_key))
                .apply();

        SharedPreferences secure = context.getApplicationContext()
                .getSharedPreferences(SECURE_PREFS, Context.MODE_PRIVATE);
        if (token.trim().isEmpty()) {
            secure.edit().remove(WRAPPED_KEY).remove(IV).remove(CIPHERTEXT).apply();
            return;
        }

        try {
            KeyStore keyStore = loadKeyStore();
            if (!keyStore.containsAlias(KEY_ALIAS)) generateWrappingKey(context);
            keyStore = loadKeyStore();

            KeyGenerator keyGenerator = KeyGenerator.getInstance("AES");
            keyGenerator.init(256);
            SecretKey aesKey = keyGenerator.generateKey();

            byte[] iv = new byte[12];
            new SecureRandom().nextBytes(iv);
            Cipher aes = Cipher.getInstance("AES/GCM/NoPadding");
            aes.init(Cipher.ENCRYPT_MODE, aesKey, new GCMParameterSpec(128, iv));
            byte[] ciphertext = aes.doFinal(token.trim().getBytes(StandardCharsets.UTF_8));

            Cipher rsa = Cipher.getInstance("RSA/ECB/PKCS1Padding");
            rsa.init(Cipher.ENCRYPT_MODE, keyStore.getCertificate(KEY_ALIAS).getPublicKey());
            byte[] wrappedKey = rsa.doFinal(aesKey.getEncoded());

            secure.edit()
                    .putString(WRAPPED_KEY, Base64.encodeToString(wrappedKey, Base64.NO_WRAP))
                    .putString(IV, Base64.encodeToString(iv, Base64.NO_WRAP))
                    .putString(CIPHERTEXT, Base64.encodeToString(ciphertext, Base64.NO_WRAP))
                    .apply();
        } catch (Exception error) {
            throw new IllegalStateException("Unable to protect bridge token with Android Keystore", error);
        }
    }

    private static KeyStore loadKeyStore() throws Exception {
        KeyStore keyStore = KeyStore.getInstance(KEYSTORE_PROVIDER);
        keyStore.load(null);
        return keyStore;
    }

    @SuppressWarnings("deprecation")
    private static void generateWrappingKey(Context context) throws Exception {
        Calendar start = Calendar.getInstance();
        Calendar end = Calendar.getInstance();
        end.add(Calendar.YEAR, 20);
        AlgorithmParameterSpec spec = new KeyPairGeneratorSpec.Builder(context.getApplicationContext())
                .setAlias(KEY_ALIAS)
                .setSubject(new X500Principal("CN=MALP YouTube Bridge Token"))
                .setSerialNumber(BigInteger.ONE)
                .setStartDate(start.getTime())
                .setEndDate(new Date(end.getTimeInMillis()))
                .setKeySize(2048)
                .build();
        KeyPairGenerator generator = KeyPairGenerator.getInstance("RSA", KEYSTORE_PROVIDER);
        generator.initialize(spec);
        generator.generateKeyPair();
    }
}
