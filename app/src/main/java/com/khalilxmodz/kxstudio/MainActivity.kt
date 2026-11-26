package com.khalilxmodz.kxstudio

import android.app.Activity
import android.content.Intent
import android.graphics.Color
import android.graphics.Typeface
import android.net.Uri
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.text.Editable
import android.text.Spannable
import android.text.SpannableString
import android.text.TextWatcher
import android.text.style.ForegroundColorSpan
import android.view.Gravity
import android.view.KeyEvent
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.EditText
import android.widget.HorizontalScrollView
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import java.util.concurrent.Executors
import java.util.regex.Pattern

class MainActivity : Activity() {
    private lateinit var editor: EditText
    private lateinit var output: TextView
    private lateinit var status: TextView
    private lateinit var compileButton: Button
    private lateinit var runButton: Button
    private var applyingHighlight = false
    private var applyingPair = false

    private val worker = Executors.newSingleThreadExecutor()
    private val mainHandler = Handler(Looper.getMainLooper())

    external fun nativeCompile(source: String): String
    external fun nativeRun(source: String): String

    companion object {
        init { System.loadLibrary("kxstudio") }
        private const val OPEN_FILE = 1001
        private const val SAVE_FILE = 1002
        private const val STATE_SOURCE = "kx_editor_source"
        private const val SAMPLE = """fn main() -> i32 {
    i32 x = 40;
    i32 y = 2;
    print(x + y);
    return 0;
}
"""
        private val KEYWORDS = Pattern.compile("\\b(fn|return|if|else|while|for|break|continue|true|false|null|struct|enum|const|let|mut|pub|import|include)\\b")
        private val TYPES = Pattern.compile("\\b(i8|i16|i32|i64|u8|u16|u32|u64|f32|f64|bool|void|string|char)\\b")
        private val NUMBERS = Pattern.compile("\\b[0-9]+(\\.[0-9]+)?\\b")
        private val STRINGS = Pattern.compile("\"([^\\\\\"]|\\\\.)*\"|'([^\\\\']|\\\\.)*'")
        private val COMMENTS = Pattern.compile("//[^\\n]*|/\\*[\\s\\S]*?\\*/")
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.statusBarColor = Color.rgb(24, 26, 34)
        window.navigationBarColor = Color.rgb(24, 26, 34)
        buildUi(savedInstanceState?.getString(STATE_SOURCE))
    }

    override fun onSaveInstanceState(outState: Bundle) {
        if (::editor.isInitialized) outState.putString(STATE_SOURCE, editor.text.toString())
        super.onSaveInstanceState(outState)
    }

    private fun buildUi(restoredSource: String?) {
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(9), dp(5), dp(9), dp(5))
            setBackgroundColor(Color.rgb(24, 26, 34))
        }

        // Compact VS Code-like top command bar; Run stays at the top.
        val toolbar = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setPadding(dp(4), 0, dp(4), 0)
        }
        val brand = TextView(this).apply {
            text = "KX"
            textSize = 19f
            typeface = Typeface.create("sans-serif", Typeface.BOLD)
            setTextColor(Color.rgb(92, 170, 255))
        }
        val fileName = TextView(this).apply {
            text = "  main.kx"
            textSize = 12f
            setTextColor(Color.rgb(205, 210, 222))
        }
        toolbar.addView(brand)
        toolbar.addView(fileName, LinearLayout.LayoutParams(0, dp(36), 1f))
        val open = smallButton("Open") { openFile() }
        val save = smallButton("Save") { saveFile() }
        compileButton = smallButton("Build") { compile() }
        runButton = smallButton("▶ Run", Color.rgb(34, 139, 91)) { runCode() }
        toolbar.addView(open)
        toolbar.addView(save)
        toolbar.addView(compileButton)
        toolbar.addView(runButton)
        root.addView(toolbar)

        status = TextView(this).apply {
            text = "● Ready     KX Compiler • ARMv7"
            textSize = 10f
            setTextColor(Color.rgb(143, 155, 174))
            setPadding(dp(7), dp(2), 0, dp(4))
        }
        root.addView(status)

        val editorFrame = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            setBackgroundColor(Color.rgb(30, 33, 43))
        }
        val lineNumbers = TextView(this).apply {
            text = (1..60).joinToString("\\n") { it.toString() }
            textSize = 13f
            typeface = Typeface.MONOSPACE
            gravity = Gravity.TOP or Gravity.END
            setTextColor(Color.rgb(95, 105, 125))
            setPadding(dp(8), dp(12), dp(8), dp(12))
            setBackgroundColor(Color.rgb(26, 29, 38))
        }
        editorFrame.addView(lineNumbers, LinearLayout.LayoutParams(dp(42), ViewGroup.LayoutParams.MATCH_PARENT))
        editor = EditText(this).apply {
            setText(restoredSource ?: SAMPLE)
            setTextColor(Color.rgb(220, 225, 235))
            setHintTextColor(Color.GRAY)
            setBackgroundColor(Color.TRANSPARENT)
            textSize = 14f
            typeface = Typeface.MONOSPACE
            gravity = Gravity.TOP or Gravity.START
            setPadding(dp(10), dp(12), dp(12), dp(12))
            setHorizontallyScrolling(true)
            isSingleLine = false
            minLines = 12
            setTextIsSelectable(true)
        }
        editorFrame.addView(editor, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 1f))
        root.addView(editorFrame, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f).apply {
            topMargin = dp(3)
            bottomMargin = dp(5)
        })

        // Lightweight inline coding help: tap a completion to insert it at the cursor.
        val helpTitle = TextView(this).apply {
            text = "SUGGESTIONS  ·  اضغط لإدراج اقتراح"
            textSize = 9f
            setTextColor(Color.rgb(126, 143, 169))
            setPadding(dp(4), dp(2), 0, dp(2))
        }
        root.addView(helpTitle)
        val suggestionScroll = HorizontalScrollView(this).apply { isHorizontalScrollBarEnabled = false }
        val suggestions = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        listOf("fn main() {\n    \n}", "i32 ", "print()", "if () {\n    \n}", "while () {\n    \n}", "return 0;", "// comment").forEach { snippet ->
            val chip = smallButton(snippet.replace("\n", "↵").take(14), Color.rgb(49, 56, 72)) {
                insertCompletion(snippet)
            }
            suggestions.addView(chip)
        }
        suggestionScroll.addView(suggestions)
        root.addView(suggestionScroll, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(34)))

        val outputHeader = TextView(this).apply {
            text = "TERMINAL / DIAGNOSTICS"
            textSize = 9f
            setTextColor(Color.rgb(126, 143, 169))
            setPadding(dp(5), dp(4), 0, dp(2))
        }
        root.addView(outputHeader)
        val outputScroll = ScrollView(this).apply {
            setBackgroundColor(Color.rgb(18, 20, 27))
            isFillViewport = true
        }
        output = TextView(this).apply {
            text = "KX Studio ready.\nBuild checks KX source and emits LLVM IR.\nRun depends on native runtime availability."
            textSize = 11f
            typeface = Typeface.MONOSPACE
            setTextColor(Color.rgb(190, 202, 218))
            setPadding(dp(8), dp(5), dp(8), dp(5))
        }
        outputScroll.addView(output)
        root.addView(outputScroll, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(66)))
        setContentView(root)

        installEditorAssistance()
        highlightEditor()
    }

    private fun smallButton(label: String, color: Int = Color.rgb(48, 53, 67), action: () -> Unit): Button =
        Button(this).apply {
            text = label
            textSize = 10f
            isAllCaps = false
            minWidth = dp(0)
            minimumWidth = dp(0)
            minHeight = dp(30)
            minimumHeight = dp(30)
            setPadding(dp(7), 0, dp(7), 0)
            setTextColor(Color.WHITE)
            setBackgroundColor(color)
            setOnClickListener { action() }
            layoutParams = LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, dp(32)).apply {
                marginStart = dp(2)
                marginEnd = dp(2)
            }
        }

    private fun dp(value: Int) = (value * resources.displayMetrics.density).toInt()

    private fun installEditorAssistance() {
        editor.addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {
                if (applyingPair || s == null || before != 0 || count != 1 || start >= s.length) return
                val closing = when (s[start]) {
                    '(' -> ")"
                    '{' -> "}"
                    '[' -> "]"
                    '"' -> "\""
                    else -> null
                } ?: return
                applyingPair = true
                editor.text.insert(start + 1, closing)
                editor.setSelection(start + 1)
                applyingPair = false
            }
            override fun afterTextChanged(s: Editable?) {
                if (!applyingHighlight) highlightEditor()
            }
        })
        editor.setOnKeyListener { _, keyCode, event ->
            if (keyCode == KeyEvent.KEYCODE_ENTER && event.action == KeyEvent.ACTION_DOWN) {
                val cursor = editor.selectionStart.coerceAtLeast(0)
                val beforeCursor = editor.text.substring(0, cursor)
                val currentLine = beforeCursor.substringAfterLast('\n')
                val indent = currentLine.takeWhile { it == ' ' || it == '\t' }
                val extra = if (currentLine.trimEnd().endsWith("{")) "    " else ""
                editor.text.insert(cursor, "\n$indent$extra")
                editor.setSelection(cursor + 1 + indent.length + extra.length)
                return@setOnKeyListener true
            }
            false
        }
    }

    private fun highlightEditor() {
        if (!::editor.isInitialized || applyingHighlight) return
        val text = editor.text.toString()
        val cursor = editor.selectionStart.coerceIn(0, text.length)
        val styled = SpannableString(text)
        fun colorMatches(pattern: Pattern, color: Int) {
            val matcher = pattern.matcher(text)
            while (matcher.find()) styled.setSpan(
                ForegroundColorSpan(color), matcher.start(), matcher.end(), Spannable.SPAN_EXCLUSIVE_EXCLUSIVE
            )
        }
        colorMatches(COMMENTS, Color.rgb(112, 153, 119))
        colorMatches(STRINGS, Color.rgb(215, 168, 112))
        colorMatches(KEYWORDS, Color.rgb(198, 120, 221))
        colorMatches(TYPES, Color.rgb(86, 183, 194))
        colorMatches(NUMBERS, Color.rgb(209, 174, 110))
        applyingHighlight = true
        editor.setText(styled, TextView.BufferType.SPANNABLE)
        editor.setSelection(cursor.coerceAtMost(editor.text.length))
        applyingHighlight = false
    }

    private fun insertCompletion(snippet: String) {
        val cursor = editor.selectionStart.coerceAtLeast(0)
        val text = editor.text
        var start = cursor
        while (start > 0 && (text[start - 1].isLetterOrDigit() || text[start - 1] == '_')) start--
        text.replace(start, cursor, snippet)
        val newCursor = start + snippet.indexOf("\n").let { if (it >= 0) it + 1 else snippet.length }
        editor.setSelection(newCursor.coerceIn(0, text.length))
        editor.requestFocus()
    }

    private fun setBusy(busy: Boolean) {
        compileButton.isEnabled = !busy
        runButton.isEnabled = !busy
        compileButton.alpha = if (busy) 0.55f else 1f
        runButton.alpha = if (busy) 0.55f else 1f
    }

    private fun executeNativeTask(startMessage: String, task: (String) -> String, resultStatus: (String) -> String) {
        val source = editor.text.toString()
        setBusy(true)
        status.text = startMessage
        worker.execute {
            val result = try { task(source) }
            catch (error: Throwable) { "NATIVE ERROR\\n${error.javaClass.simpleName}: ${error.message ?: "Unknown error"}" }
            mainHandler.post {
                if (isFinishing || isDestroyed) return@post
                output.text = result
                status.text = resultStatus(result)
                setBusy(false)
            }
        }
    }

    private fun compile() {
        executeNativeTask(
            "Building KX source…",
            { source -> nativeCompile(source) },
            { result -> if (result.startsWith("KX FRONTEND: SUCCESS")) "Build succeeded • LLVM IR ready" else "Build failed • See diagnostics" }
        )
    }

    private fun runCode() {
        executeNativeTask(
            "Running KX program…",
            { source -> nativeRun(source) },
            { result -> when {
                result.startsWith("NATIVE ARMv7 RUN: SUCCESS") -> "Native ARMv7 execution succeeded"
                result.startsWith("NATIVE ARMv7 RUN: FAILED") -> "Native execution failed • See output"
                else -> "Runtime unavailable • See output"
            } }
        )
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
                OPEN_FILE -> {
                    val contents = contentResolver.openInputStream(uri)?.bufferedReader()?.use { it.readText() }
                    if (contents != null) editor.setText(contents) else throw IllegalStateException("Unable to open selected file")
                }
                SAVE_FILE -> {
                    val stream = contentResolver.openOutputStream(uri) ?: throw IllegalStateException("Unable to create selected file")
                    stream.bufferedWriter().use { it.write(editor.text.toString()) }
                }
            }
            Toast.makeText(this, "File operation completed", Toast.LENGTH_SHORT).show()
        } catch (error: Exception) {
            Toast.makeText(this, "File error: ${error.message}", Toast.LENGTH_LONG).show()
        }
    }

    override fun onDestroy() {
        worker.shutdownNow()
        super.onDestroy()
    }
}
