package com.williandlima.fontecontrol.model

import org.json.JSONObject

/** Leitura periodica enviada pelo firmware ({"t":"m",...}). */
data class Measurement(
    val tMs: Long,
    val v: Double,
    val i: Double,
    val out: Boolean,
    val ovpTrip: Boolean,
    val ocpTrip: Boolean,
) {
    val p: Double get() = v * i
}

/** Configuracao programada na fonte ({"t":"st",...}). */
data class PsuSettings(
    val range: String = "P25V",
    val vset: Double = 0.0,
    val iset: Double = 0.0,
    val out: Boolean = false,
    val ovp: Double = 55.0,
    val ovpEn: Boolean = false,
    val ocp: Double = 7.5,
    val ocpEn: Boolean = false,
    val disp: Boolean = true,
    val trigSrc: String = "IMM",
    val trigDel: Double = 0.0,
    val vtrig: Double = 0.0,
    val itrig: Double = 0.0,
    val pollMs: Int = 500,
) {
    val vMax: Double get() = if (range == "P50V") 51.5 else 25.75
    val iMax: Double get() = if (range == "P50V") 4.12 else 7.21
}

sealed interface Incoming {
    data class Hello(val link: Boolean, val idn: String?, val pollMs: Int, val fw: String?) : Incoming
    data class Link(val ok: Boolean, val idn: String?, val err: String?) : Incoming
    data class Meas(val m: Measurement) : Incoming
    data class Status(val s: PsuSettings) : Incoming
    data class Resp(val id: Int?, val ok: Boolean, val cmd: String?, val data: String?, val err: String?) : Incoming
    data class Error(val code: Int, val msg: String) : Incoming
}

object Protocol {

    fun cmd(name: String, id: Int, vararg args: Pair<String, Any?>): String {
        val o = JSONObject()
        o.put("cmd", name)
        o.put("id", id)
        for ((k, v) in args) if (v != null) o.put(k, v)
        return o.toString()
    }

    fun parse(line: String, nowMs: Long = System.currentTimeMillis()): Incoming? {
        val o = try {
            JSONObject(line)
        } catch (e: Exception) {
            return null
        }
        return when (o.optString("t")) {
            "hello" -> Incoming.Hello(
                o.optBoolean("link"), o.optString("idn").ifEmpty { null }, o.optInt("poll", 500),
                o.optString("fw").ifEmpty { null },
            )
            "link" -> Incoming.Link(
                o.optBoolean("ok"),
                o.optString("idn").ifEmpty { null },
                o.optString("err").ifEmpty { null },
            )
            "m" -> Incoming.Meas(
                Measurement(
                    nowMs, o.optDouble("v"), o.optDouble("i"), o.optBoolean("o"),
                    o.optBoolean("ovp_t"), o.optBoolean("ocp_t"),
                )
            )
            "st" -> Incoming.Status(
                PsuSettings(
                    range = o.optString("range", "P25V"),
                    vset = o.optDouble("vset"),
                    iset = o.optDouble("iset"),
                    out = o.optBoolean("out"),
                    ovp = o.optDouble("ovp"),
                    ovpEn = o.optBoolean("ovp_en"),
                    ocp = o.optDouble("ocp"),
                    ocpEn = o.optBoolean("ocp_en"),
                    disp = o.optBoolean("disp", true),
                    trigSrc = o.optString("trig_src", "IMM"),
                    trigDel = o.optDouble("trig_del", 0.0),
                    vtrig = o.optDouble("vtrig", 0.0),
                    itrig = o.optDouble("itrig", 0.0),
                    pollMs = o.optInt("poll", 500),
                )
            )
            "r" -> Incoming.Resp(
                if (o.has("id")) o.optInt("id") else null,
                o.optBoolean("ok"),
                o.optString("cmd").ifEmpty { null },
                if (o.has("d")) o.get("d").toString() else null,
                o.optString("err").ifEmpty { null },
            )
            "e" -> Incoming.Error(o.optInt("code"), o.optString("msg"))
            else -> null
        }
    }
}
