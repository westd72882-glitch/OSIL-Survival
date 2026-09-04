package com.osil.survival;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;

/**
 * Единственная задача этого класса — открыть https, потому что делать это в C++ нечем:
 * своей криптографии в игре нет, а тащить в APK целую TLS-библиотеку ради одного запроса
 * в сто миллисекунд не стоит. Android умеет это сам, вместе с системным списком
 * доверенных сертификатов, который обновляется прошивкой.
 *
 * Вызывается из нативного кода (src/Net/Http.cpp) и работает в его же потоке, поэтому
 * здесь нет ни потоков, ни очередей: метод блокирующий и с таймаутом.
 *
 * Ответ отдаётся строкой «код\nтело»: возвращать через JNI пару значений заметно
 * муторнее, а разобрать одну строку — три строки кода.
 */
public final class Https {
    private Https() {}

    public static String request(String url, String method, String body, int timeoutMs) {
        HttpURLConnection connection = null;
        try {
            connection = (HttpURLConnection) new URL(url).openConnection();
            connection.setConnectTimeout(timeoutMs);
            connection.setReadTimeout(timeoutMs);
            connection.setRequestMethod(method == null ? "GET" : method);
            connection.setRequestProperty("Content-Type", "application/json");
            connection.setRequestProperty("Accept", "application/json");
            connection.setRequestProperty("User-Agent", "OSILSurvival/1.0");
            // Хостинги любят отвечать перенаправлением с http на https — идём по нему сами.
            connection.setInstanceFollowRedirects(true);

            if (body != null && body.length() > 0 && !"GET".equals(method)) {
                byte[] data = body.getBytes("UTF-8");
                connection.setDoOutput(true);
                connection.setFixedLengthStreamingMode(data.length);
                OutputStream out = connection.getOutputStream();
                out.write(data);
                out.flush();
                out.close();
            }

            int status = connection.getResponseCode();
            InputStream in = (status >= 400) ? connection.getErrorStream()
                                             : connection.getInputStream();
            ByteArrayOutputStream buffer = new ByteArrayOutputStream();
            if (in != null) {
                byte[] chunk = new byte[4096];
                int read;
                while ((read = in.read(chunk)) > 0) buffer.write(chunk, 0, read);
                in.close();
            }
            return status + "\n" + buffer.toString("UTF-8");
        } catch (Throwable t) {
            // Наружу отдаём null: нативная сторона покажет игроку «сервер не отвечает».
            return null;
        } finally {
            if (connection != null) connection.disconnect();
        }
    }
}
