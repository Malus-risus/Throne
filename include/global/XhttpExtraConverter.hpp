#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QString>

class XhttpExtraConverter
{
public:
    static void mergeQJsonObject(QJsonObject &obj1, const QJsonObject &obj2)
    {
        for (auto it = obj2.constBegin(); it != obj2.constEnd(); it++) {
            obj1.insert(it.key(), it.value());
        }
    }

    static QJsonObject xrayToSingBox(const QString &xrayExtra)
    {
        if (xrayExtra.trimmed().isEmpty()) return {};

        QJsonParseError err{};
        const auto doc = QJsonDocument::fromJson(xrayExtra.toUtf8(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            return {};
        }

        const QJsonObject xray = doc.object();
        if (isSingBoxFormat(xray)) return xray;

        QJsonObject singBox;

        convertField(xray, singBox, "xPaddingBytes",           "x_padding_bytes");
        convertField(xray, singBox, "scMaxEachPostBytes",      "sc_max_each_post_bytes");
        convertField(xray, singBox, "scMinPostsIntervalMs",   "sc_min_posts_interval_ms");
        convertField(xray, singBox, "noGRPCHeader",           "no_grpc_header");

        // xmux
        if (xray.contains("xmux") && xray["xmux"].isObject()) {
            singBox["xmux"] = convertXrayXmux(xray["xmux"].toObject());
        }

        // downloadSettings → download
        if (xray.contains("downloadSettings") && xray["downloadSettings"].isObject()) {
            const QJsonObject xDown = xray["downloadSettings"].toObject();
            QJsonObject sDown;

            // xhttpSettings (mode/host/path)
            if (xDown.contains("xhttpSettings") && xDown["xhttpSettings"].isObject()) {
                const QJsonObject xhttp = xDown["xhttpSettings"].toObject();
                convertField(xhttp, sDown, "mode", "mode");
                convertField(xhttp, sDown, "host", "host");
                convertField(xhttp, sDown, "path", "path");
            }

            convertField(xDown, sDown, "address", "server");
            convertField(xDown, sDown, "port",    "server_port");

            // TLS / Reality
            if (xDown.contains("security") && xDown["security"].isString()) {
                QJsonObject tls{ {"enabled", true} };
                const QString security = xDown["security"].toString();

                if (security == "tls") {
                    if (xDown.contains("tlsSettings") && xDown["tlsSettings"].isObject()) {
                        const QJsonObject tlsSet = xDown["tlsSettings"].toObject();
                        convertField(tlsSet, tls, "serverName",     "server_name");
                        convertField(tlsSet, tls, "alpn",           "alpn");
                        convertField(tlsSet, tls, "allowInsecure",  "insecure");

                        if (tlsSet.contains("fingerprint") && !tlsSet["fingerprint"].toString().isEmpty()) {
                            QJsonObject utls{ {"enabled", true}, {"fingerprint", tlsSet["fingerprint"]} };
                            tls["utls"] = utls;
                        }
                    }
                } else if (security == "reality") {
                    if (xDown.contains("realitySettings") && xDown["realitySettings"].isObject()) {
                        const QJsonObject realSet = xDown["realitySettings"].toObject();
                        convertField(realSet, tls, "serverName", "server_name");

                        QJsonObject reality{ {"enabled", true} };
                        convertField(realSet, reality, "publicKey", "public_key");
                        convertField(realSet, reality, "shortId",   "short_id");
                        tls["reality"] = reality;

                        if (realSet.contains("fingerprint") && !realSet["fingerprint"].toString().isEmpty()) {
                            QJsonObject utls{ {"enabled", true}, {"fingerprint", realSet["fingerprint"]} };
                            tls["utls"] = utls;
                        }
                    }
                }
                sDown["tls"] = tls;
            }

            // downloadSettings.xhttpSettings.extra → download + xmux
            if (xDown.contains("xhttpSettings") && xDown["xhttpSettings"].isObject()) {
                const QJsonObject xhttp = xDown["xhttpSettings"].toObject();
                if (xhttp.contains("extra") && xhttp["extra"].isObject()) {
                    const QJsonObject extra = xhttp["extra"].toObject();

                    convertField(extra, sDown, "xPaddingBytes",         "x_padding_bytes");
                    convertField(extra, sDown, "scMaxEachPostBytes",    "sc_max_each_post_bytes");
                    convertField(extra, sDown, "scMinPostsIntervalMs", "sc_min_posts_interval_ms");
                    convertField(extra, sDown, "noGRPCHeader",         "no_grpc_header");

                    if (extra.contains("xmux") && extra["xmux"].isObject()) {
                        sDown["xmux"] = convertXrayXmux(extra["xmux"].toObject());
                    }
                }
            }

            if (!sDown.isEmpty()) {
                singBox["download"] = sDown;
            }
        }

        return singBox;
    }

    static QString singBoxToXray(const QJsonObject &sing)
    {
        if (isXrayFormat(sing)) return QJsonDocument(sing).toJson(QJsonDocument::Indented).replace("\\/", "/");

        QJsonObject xray;

        convertField(sing, xray, "x_padding_bytes",          "xPaddingBytes");
        convertField(sing, xray, "sc_max_each_post_bytes",   "scMaxEachPostBytes");
        convertField(sing, xray, "sc_min_posts_interval_ms", "scMinPostsIntervalMs");
        convertField(sing, xray, "no_grpc_header",          "noGRPCHeader");

        if (sing.contains("xmux") && sing["xmux"].isObject()) {
            xray["xmux"] = convertSingBoxXmux(sing["xmux"].toObject());
        }

        // download → downloadSettings
        if (sing.contains("download") && sing["download"].isObject()) {
            const QJsonObject sDown = sing["download"].toObject();
            QJsonObject xDown;

            convertField(sDown, xDown, "server",      "address");
            convertField(sDown, xDown, "server_port", "port");
            xDown["network"] = "xhttp";

            // TLS / Reality
            if (sDown.contains("tls") && sDown["tls"].isObject()) {
                const QJsonObject tls = sDown["tls"].toObject();

                if (tls.contains("reality") && tls["reality"].isObject() &&
                    tls["reality"].toObject().value("enabled").toBool(false)) {

                    xDown["security"] = "reality";
                    QJsonObject realitySettings;
                    convertField(tls, realitySettings, "server_name", "serverName");

                    const QJsonObject reality = tls["reality"].toObject();
                    convertField(reality, realitySettings, "public_key", "publicKey");
                    convertField(reality, realitySettings, "short_id",   "shortId");

                    if (tls.contains("utls") && tls["utls"].isObject()) {
                        convertField(tls["utls"].toObject(), realitySettings, "fingerprint", "fingerprint");
                    }
                    xDown["realitySettings"] = realitySettings;
                } else {
                    xDown["security"] = "tls";
                    QJsonObject tlsSettings;
                    convertField(tls, tlsSettings, "server_name",    "serverName");
                    convertField(tls, tlsSettings, "alpn",          "alpn");
                    convertField(tls, tlsSettings, "insecure",      "allowInsecure");

                    if (tls.contains("utls") && tls["utls"].isObject()) {
                        convertField(tls["utls"].toObject(), tlsSettings, "fingerprint", "fingerprint");
                    }
                    xDown["tlsSettings"] = tlsSettings;
                }
            }

            // mode/host/path + extra
            QJsonObject xhttpSettings;
            convertField(sDown, xhttpSettings, "mode", "mode");
            convertField(sDown, xhttpSettings, "host", "host");
            convertField(sDown, xhttpSettings, "path", "path");

            QJsonObject xhttpExtra;
            convertField(sDown, xhttpExtra, "x_padding_bytes",          "xPaddingBytes");
            convertField(sDown, xhttpExtra, "sc_max_each_post_bytes",   "scMaxEachPostBytes");
            convertField(sDown, xhttpExtra, "sc_min_posts_interval_ms", "scMinPostsIntervalMs");
            convertField(sDown, xhttpExtra, "no_grpc_header",          "noGRPCHeader");

            if (sDown.contains("xmux") && sDown["xmux"].isObject()) {
                xhttpExtra["xmux"] = convertSingBoxXmux(sDown["xmux"].toObject());
            }

            if (!xhttpExtra.isEmpty()) {
                xhttpSettings["extra"] = xhttpExtra;
            }
            xDown["xhttpSettings"] = xhttpSettings;

            if (!xDown.isEmpty()) {
                xray["downloadSettings"] = xDown;
            }
        }

        return QJsonDocument(xray).toJson(QJsonDocument::Indented).replace("\\/", "/");
    }

private:
    static bool isSingBoxFormat(const QJsonObject &json)
    {
        return json.contains("x_padding_bytes") ||
               json.contains("sc_max_each_post_bytes") ||
               json.contains("sc_min_posts_interval_ms") ||
               json.contains("sc_stream_up_server_secs") ||
               json.contains("download");
    }

    static bool isXrayFormat(const QJsonObject &json)
    {
        return json.contains("xPaddingBytes") ||
               json.contains("scMaxEachPostBytes") ||
               json.contains("scMinPostsIntervalMs") ||
               json.contains("scStreamUpServerSecs") ||
               json.contains("downloadSettings");
    }

    static void convertField(const QJsonObject &from, QJsonObject &to,
                             const QString &fromKey, const QString &toKey)
    {
        if (from.contains(fromKey)) {
            to[toKey] = from[fromKey];
        }
    }

    static QJsonObject convertXrayXmux(const QJsonObject &xrayXmux)
    {
        QJsonObject sing;
        convertField(xrayXmux, sing, "maxConcurrency",     "max_concurrency");
        convertField(xrayXmux, sing, "maxConnections",     "max_connections");
        convertField(xrayXmux, sing, "cMaxReuseTimes",     "c_max_reuse_times");
        convertField(xrayXmux, sing, "hMaxRequestTimes",   "h_max_request_times");
        convertField(xrayXmux, sing, "hMaxReusableSecs",   "h_max_reusable_secs");
        convertField(xrayXmux, sing, "hKeepAlivePeriod",   "h_keep_alive_period");
        return sing;
    }

    static QJsonObject convertSingBoxXmux(const QJsonObject &singXmux)
    {
        QJsonObject xray;
        convertField(singXmux, xray, "max_concurrency",     "maxConcurrency");
        convertField(singXmux, xray, "max_connections",    "maxConnections");
        convertField(singXmux, xray, "c_max_reuse_times",  "cMaxReuseTimes");
        convertField(singXmux, xray, "h_max_request_times","hMaxRequestTimes");
        convertField(singXmux, xray, "h_max_reusable_secs","hMaxReusableSecs");
        convertField(singXmux, xray, "h_keep_alive_period","hKeepAlivePeriod");
        return xray;
    }
};