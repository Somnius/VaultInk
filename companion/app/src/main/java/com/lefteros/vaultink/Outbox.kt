package com.lefteros.vaultink

import android.content.Context
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

/**
 * Notes and shared files kept on the phone until the reader can be reached. Each item is a folder
 * in the app's files: `item.json` plus its files (a note's pictures, or the shared book). Items
 * survive the app closing; they're sent oldest first when the reader is found, and each one is
 * deleted only after the reader accepted it.
 */
object Outbox {
    private fun root(context: Context) = File(context.filesDir, "outbox").apply { mkdirs() }

    private fun newItem(context: Context): File {
        val dir = File(root(context), "${System.currentTimeMillis()}-${(1000..9999).random()}")
        dir.mkdirs()
        return dir
    }

    /** A note for `/Vaults/<vault>/<folder>`, with its pictures (moved into the outbox). */
    fun addNote(context: Context, vault: String, folder: String, title: String, text: String, pictures: List<File>) {
        val dir = newItem(context)
        val names = JSONArray()
        for (p in pictures) {
            val dest = File(dir, p.name)
            if (!p.renameTo(dest)) p.copyTo(dest, overwrite = true)
            names.put(p.name)
        }
        val json = JSONObject().put("type", "note").put("vault", vault).put("folder", folder)
            .put("title", title).put("text", text).put("pictures", names)
        File(dir, "item.json").writeText(json.toString())
    }

    /** A shared book or comic for `folder` on the card (`/Books`, `/Comics`). */
    fun addFile(context: Context, folder: String, file: File) {
        val dir = newItem(context)
        val dest = File(dir, file.name)
        if (!file.renameTo(dest)) file.copyTo(dest, overwrite = true)
        File(dir, "item.json").writeText(JSONObject().put("type", "file").put("folder", folder).put("name", file.name).toString())
    }

    private fun items(context: Context): List<File> =
        root(context).listFiles()?.filter { File(it, "item.json").isFile }?.sortedBy { it.name }.orEmpty()

    fun count(context: Context): Int = items(context).size

    /** Sends what's waiting; stops at the first failure (the reader went away). Returns how many were sent. */
    fun send(context: Context, client: DeviceClient): Int {
        var sent = 0
        for (dir in items(context)) {
            try {
                val json = JSONObject(File(dir, "item.json").readText())
                when (json.optString("type")) {
                    "note" -> sendNote(client, dir, json)
                    "file" -> {
                        val folder = json.getString("folder")
                        val file = File(dir, json.getString("name"))
                        if (!file.isFile) throw org.json.JSONException("file missing")  // nothing left to send
                        client.ensureFolder(folder)
                        client.upload(folder, file.name, file.length(), { file.inputStream() }) { _, _ -> }
                    }
                }
            } catch (e: org.json.JSONException) {
                // A broken item would block the queue for ever: drop it.
            } catch (e: Exception) {
                // The reader went away (IOException) or something unexpected: keep it for next time.
                return sent
            }
            dir.deleteRecursively()
            sent++
        }
        return sent
    }

    private fun sendNote(client: DeviceClient, dir: File, json: JSONObject) {
        val vaultPath = "/Vaults/" + json.getString("vault")
        val notePath = "$vaultPath/" + json.getString("folder").trim('/')
        val title = json.getString("title")
        client.ensureFolder(notePath)
        val links = StringBuilder()
        val pictures = json.optJSONArray("pictures") ?: JSONArray()
        if (pictures.length() > 0) client.ensureFolder("$vaultPath/attachments")
        for (i in 0 until pictures.length()) {
            val p = File(dir, pictures.getString(i))
            if (!p.isFile) continue
            val saved = client.upload("$vaultPath/attachments", p.name, p, "application/octet-stream")
            links.append("\n![[").append(saved).append("]]")
        }
        val body = noteBody(title, json.getString("text"), links.toString())
        client.upload(notePath, "$title.md", body.toByteArray(), "text/markdown")
    }

    fun noteBody(title: String, text: String, links: String): String =
        "# $title\n\n${text.trimEnd()}\n" + (if (links.isEmpty()) "" else "$links\n")
}
