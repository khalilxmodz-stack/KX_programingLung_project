package com.khalilxmodz.kxstudio

import android.app.Activity
import android.content.Intent
import android.graphics.Color
import android.graphics.Typeface
import android.net.Uri
import android.os.Bundle
import android.view.Gravity
import android.view.ViewGroup
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast

class MainActivity : Activity() {
    private lateinit var editor: EditText
    private lateinit var output: TextView
    private lateinit var status: TextView

    external fun nativeCompile(source: String): String
    external fun nativeRun(source: String): String

    companion object {
        init { System.loadLibrary("kxstudio") }
        private const val OPEN_FILE = 1001
        private const val SAVE_FILE = 1002
        private const val SAMPLE = """fn main() -> i32 {
    i32 x = 40;
    i32 y = 2;
    print(x + y);
    return 0;
}
"""
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.statusBarColor = Color.rgb(15, 17, 25)
        window.navigationBarColor = Color.rgb(15, 17, 25)
        window.decorView.systemUiVisibility = 0
        buildUi()
    }

    private fun buildUi() {
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(14), dp(14), dp(14), dp(10))
            setBackgroundColor(Color.rgb(15, 17, 25))
        }
        val header = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL }
        val brand = TextView(this).apply {
            text = "KX Studio"
            textSize = 23f
            setTextColor(Color.WHITE)
            typeface = Typeface.create("sans-serif", Typeface.BOLD)
        }
        val version = TextView(this).apply {
            text = "  v0.2-dev • ARMv7"
            textSize = 11f
            setTextColor(Color.rgb(174, 151, 255))
        }
        header.addView(brand)
        header.addView(version)
        root.addView(header)
        status = TextView(this).apply {
            text = "KX Compiler v0.21 • Ready"
            textSize = 12f
            setTextColor(Color.rgb(150, 160, 180))
            setPadding(0, dp(5), 0, dp(10))
        }
        root.addView(status)

        val fileRow = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        fileRow.addView(actionButton("Open", Color.rgb(42, 46, 62)) { openFile() }, weight())
        fileRow.addView(actionButton("Save", Color.rgb(42, 46, 62)) { saveFile() }, weight())
        root.addView(fileRow)

        editor = EditText(this).apply {
            setText(SAMPLE)
            setTextColor(Color.rgb(225, 230, 245))
            setHintTextColor(Color.GRAY)
            setBackgroundColor(Color.rgb(23, 26, 38))
            textSize = 14f
            typeface = Typeface.MONOSPACE
            gravity = Gravity.TOP or Gravity.START
            setPadding(dp(12), dp(12), dp(12), dp(12))
            setHorizontallyScrolling(true)
            isSingleLine = false
            minLines = 11
            maxLines = 24
        }
        root.addView(editor, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f).apply { topMargin = dp(10); bottomMargin = dp(10) })

        val actions = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        actions.addView(actionButton("Compile", Color.rgb(105, 70, 205)) { compile() }, weight())
        actions.addView(actionButton("Run", Color.rgb(35, 145, 105)) { runCode() }, weight())
        root.addView(actions)

        val outputTitle = TextView(this).apply {
            text = "OUTPUT / DIAGNOSTICS"
            textSize = 11f
            setTextColor(Color.rgb(174, 151, 255))
            setPadding(0, dp(12), 0, dp(6))
            typeface = Typeface.DEFAULT_BOLD
        }
        root.addView(outputTitle)
        val outputScroll = ScrollView(this).apply {
            setBackgroundColor(Color.rgb(8, 10, 16))
            isFillViewport = true
        }
        output = TextView(this).apply {
            text = "Welcome to KX Studio.\nCompile validates KX source and emits ARMv7-targeted LLVM IR.\nNative Run is disabled until the real Android runtime is integrated."
            textSize = 12f
            typeface = Typeface.MONOSPACE
            setTextColor(Color.rgb(190, 205, 220))
            setPadding(dp(10), dp(10), dp(10), dp(10))
        }
        outputScroll.addView(output)
        root.addView(outputScroll, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(145)))
        setContentView(root)
    }

    private fun actionButton(label: String, color: Int, action: () -> Unit): Button = Button(this).apply {
        text = label
        textSize = 12f
        isAllCaps = false
        setTextColor(Color.WHITE)
        setBackgroundColor(color)
        setOnClickListener { action() }
        minHeight = dp(44)
    }

    private fun weight() = LinearLayout.LayoutParams(0, dp(46), 1f).apply { marginEnd = dp(5); marginStart = dp(5) }
    private fun dp(v: Int) = (v * resources.displayMetrics.density).toInt()

    private fun compile() {
        status.text = "Compiling with KX Compiler v0.21…"
        val result = nativeCompile(editor.text.toString())
        output.text = result
        status.text = if (result.startsWith("KX FRONTEND: SUCCESS")) "KX frontend + LLVM IR succeeded" else "Compile failed • See diagnostics"
    }

    private fun runCode() {
        status.text = "Checking native runtime…"
        val result = nativeRun(editor.text.toString())
        output.text = result
        status.text = "Native runtime not integrated • See output"
    }

    private fun openFile() {
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = "*/*"
            putExtra(Intent.EXTRA_MIME_TYPES, arrayOf("text/plain", "text/x-c", "application/octet-stream"))
        }
        startActivityForResult(intent, OPEN_FILE)
    }

    private fun saveFile() {
        val intent = Intent(Intent.ACTION_CREATE_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = "text/plain"
            putExtra(Intent.EXTRA_TITLE, "main.kx")
        }
        startActivityForResult(intent, SAVE_FILE)
    }

    @Deprecated("Using activity result callbacks for broad Android compatibility")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (resultCode != Activity.RESULT_OK) return
        val uri: Uri = data?.data ?: return
        try {
            when (requestCode) {
                OPEN_FILE -> contentResolver.openInputStream(uri)?.bufferedReader()?.use { editor.setText(it.readText()) }
                SAVE_FILE -> contentResolver.openOutputStream(uri)?.bufferedWriter()?.use { it.write(editor.text.toString()) }
            }
            Toast.makeText(this, "File operation completed", Toast.LENGTH_SHORT).show()
        } catch (e: Exception) {
            Toast.makeText(this, "File error: ${e.message}", Toast.LENGTH_LONG).show()
        }
    }
}
