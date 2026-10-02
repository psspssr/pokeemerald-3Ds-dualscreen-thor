package com.emerald3ds.android

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Path
import android.graphics.Rect
import android.graphics.RectF
import android.os.VibrationEffect
import android.os.Vibrator
import android.util.SparseArray
import android.view.MotionEvent
import android.view.View
import kotlin.math.atan2
import kotlin.math.hypot
import kotlin.math.max
import kotlin.math.min

/**
 * Transparent view over a game surface. It draws the on-screen controls and
 * routes every pointer either to one control or to the bottom screen, so a
 * finger on the D-pad and another on the touch screen work together and
 * touches on controls never reach the screen.
 *
 * In a Presentation on a second display it has no controls and only maps
 * touches on the bottom screen.
 */
@SuppressLint("ViewConstructor")
class ControlsOverlayView(
    context: Context,
    private val inputSource: Int,
    private val hasControls: Boolean,
) : View(context) {

    interface Listener {
        fun onToggleBottomScreen()
        fun onControlsVisibilityChanged()
    }

    var listener: Listener? = null

    private enum class Kind { BUTTON, DPAD, CIRCLE, TOGGLE }

    private class Control(val kind: Kind, val key: Int, val label: String) {
        val bounds = RectF()
        val round: Boolean get() = kind != Kind.BUTTON || key and (CtrKeys.A or CtrKeys.B or CtrKeys.X or CtrKeys.Y) != 0

        fun contains(x: Float, y: Float, slop: Float): Boolean {
            if (bounds.isEmpty) return false
            if (!round) {
                val dx = (bounds.width() * (slop - 1f)) / 2f
                val dy = (bounds.height() * (slop - 1f)) / 2f
                return x >= bounds.left - dx && x <= bounds.right + dx && y >= bounds.top - dy && y <= bounds.bottom + dy
            }
            val r = bounds.width() / 2f * slop
            return hypot(x - bounds.centerX(), y - bounds.centerY()) <= r
        }
    }

    private sealed class Binding {
        object Touch : Binding()
        object Ignored : Binding()
        class Button(var control: Control?) : Binding()
        class Dpad(var keys: Int) : Binding()
        class Circle(var x: Int, var y: Int) : Binding()
    }

    private val dpad = Control(Kind.DPAD, 0, "")
    private val circle = Control(Kind.CIRCLE, 0, "")
    private val toggle = Control(Kind.TOGGLE, 0, context.getString(R.string.toggle_screen))
    private val buttons = listOf(
        Control(Kind.BUTTON, CtrKeys.A, "A"),
        Control(Kind.BUTTON, CtrKeys.B, "B"),
        Control(Kind.BUTTON, CtrKeys.X, "X"),
        Control(Kind.BUTTON, CtrKeys.Y, "Y"),
        Control(Kind.BUTTON, CtrKeys.L, "L"),
        Control(Kind.BUTTON, CtrKeys.R, "R"),
        Control(Kind.BUTTON, CtrKeys.START, "START"),
        Control(Kind.BUTTON, CtrKeys.SELECT, "SELECT"),
    )
    private fun button(key: Int) = buttons.first { it.key == key }

    private val pointers = SparseArray<Binding>()
    private var pressedKeys = 0
    private var circleX = 0
    private var circleY = 0

    private var settings: AppSettings? = null
    private var topRect: Rect? = null
    private var bottomRect: Rect? = null
    private var safe = Rect()
    private var showToggle = false
    /** Controls allowed by the settings for the current display mode. */
    private var allowed = true
    /** Hidden after physical input (or in dual-display mode) until the screen is touched. */
    var autoHidden = false
        private set

    val controlsVisible: Boolean get() = hasControls && allowed && !autoHidden

    private val fill = Paint(Paint.ANTI_ALIAS_FLAG)
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        textAlign = Paint.Align.CENTER
        isFakeBoldText = true
    }
    private val path = Path()
    private val tmp = RectF()
    private val vibrator: Vibrator? = context.getSystemService(Vibrator::class.java)

    fun configure(
        settings: AppSettings, top: Rect?, bottom: Rect?, safe: Rect,
        showToggle: Boolean, allowed: Boolean,
    ) {
        this.settings = settings
        topRect = top
        bottomRect = bottom
        this.safe = Rect(safe)
        this.showToggle = showToggle
        val wasVisible = controlsVisible
        this.allowed = allowed
        if (!controlsVisible) releaseControls()
        layoutControls()
        invalidate()
        if (wasVisible != controlsVisible) listener?.onControlsVisibilityChanged()
    }

    fun setAutoHidden(hidden: Boolean) {
        if (hidden == autoHidden) return
        val wasVisible = controlsVisible
        autoHidden = hidden
        if (!controlsVisible) releaseControls()
        invalidate()
        if (wasVisible != controlsVisible) listener?.onControlsVisibilityChanged()
    }

    /** Height the portrait layout keeps free under the screens. */
    fun portraitReserve(scale: Float): Int {
        val u = resources.displayMetrics.density * scale
        return ((SHOULDER_H + DPAD_D + SMALL_H + 4 * MARGIN) * u).toInt()
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        layoutControls()
    }

    private fun layoutControls() {
        val s = settings ?: return
        val w = width.toFloat()
        val h = height.toFloat()
        if (w <= 0f || h <= 0f) return
        val u = resources.displayMetrics.density * s.controlsScale
        val m = MARGIN * u
        val left = safe.left + m
        val right = w - safe.right - m
        val bottomEdge = h - safe.bottom - m
        var d = DPAD_D * u
        var c = if (s.circlePad) CIRCLE_D * u else 0f
        val b = BUTTON_D * u
        val cluster = b * 2.6f
        val shoulderW = SHOULDER_W * u
        val shoulderH = SHOULDER_H * u
        val smallW = SMALL_W * u
        val smallH = SMALL_H * u
        val portrait = h >= w
        val smallY = bottomEdge - smallH
        val clusterCy: Float
        val shoulderY: Float
        if (portrait) {
            val regionTop = max(bottomRect?.bottom ?: 0, topRect?.bottom ?: 0).toFloat()
            val freeTop = regionTop + shoulderH + 2 * m
            val freeBottom = smallY - m
            val stack = d + if (c > 0f) c + m else 0f
            if (stack > freeBottom - freeTop && freeBottom - freeTop > d * 0.6f && c > 0f) {
                val k = (freeBottom - freeTop) / stack
                d *= k
                c *= k
            }
            val leftStack = d + if (c > 0f) c + m else 0f
            val leftCenter = max(freeTop + leftStack / 2f, (freeTop + freeBottom) / 2f)
                .coerceAtMost(freeBottom - leftStack / 2f)
            clusterCy = if (c > 0f) leftCenter + leftStack / 2f - d / 2f else leftCenter
            shoulderY = min(regionTop + m, clusterCy - leftStack / 2f - m - shoulderH).coerceAtLeast(safe.top + m)
            if (c > 0f) circle.bounds.set(left + (d - c) / 2f, clusterCy - d / 2f - m - c, left + (d + c) / 2f, clusterCy - d / 2f - m)
        } else {
            clusterCy = bottomEdge - smallH - m - max(d, cluster) / 2f
            shoulderY = safe.top + m
            if (c > 0f) {
                circle.bounds.set(left + (d - c) / 2f, clusterCy - d / 2f - m - c, left + (d + c) / 2f, clusterCy - d / 2f - m)
            }
        }
        if (c <= 0f) circle.bounds.setEmpty()
        dpad.bounds.set(left, clusterCy - d / 2f, left + d, clusterCy + d / 2f)

        val ccx = right - cluster / 2f
        val off = cluster / 2f - b / 2f
        fun place(key: Int, cx: Float, cy: Float, bw: Float, bh: Float) =
            button(key).bounds.set(cx - bw / 2f, cy - bh / 2f, cx + bw / 2f, cy + bh / 2f)
        place(CtrKeys.A, ccx + off, clusterCy, b, b)
        place(CtrKeys.B, ccx, clusterCy + off, b, b)
        place(CtrKeys.X, ccx, clusterCy - off, b, b)
        place(CtrKeys.Y, ccx - off, clusterCy, b, b)

        var lTop = shoulderY
        var rTop = shoulderY
        /* Keep L and R off a bottom screen drawn in a top corner. */
        bottomRect?.let { br ->
            if (!portrait && RectF(br).intersects(left, lTop, left + shoulderW, lTop + shoulderH)) lTop = br.bottom + m
            if (!portrait && RectF(br).intersects(right - shoulderW, rTop, right, rTop + shoulderH)) rTop = br.bottom + m
        }
        button(CtrKeys.L).bounds.set(left, lTop, left + shoulderW, lTop + shoulderH)
        button(CtrKeys.R).bounds.set(right - shoulderW, rTop, right, rTop + shoulderH)

        val mid = (safe.left + w - safe.right) / 2f
        button(CtrKeys.SELECT).bounds.set(mid - m / 2f - smallW, smallY, mid - m / 2f, smallY + smallH)
        button(CtrKeys.START).bounds.set(mid + m / 2f, smallY, mid + m / 2f + smallW, smallY + smallH)

        if (showToggle) {
            val tw = TOGGLE_W * u
            toggle.bounds.set(mid - tw / 2f, safe.top + m, mid + tw / 2f, safe.top + m + smallH)
        } else {
            toggle.bounds.setEmpty()
        }
    }

    /** Where a button (by key) or the D-pad (key 0) is drawn, for tests. */
    internal fun controlBounds(key: Int): RectF = RectF(if (key == 0) dpad.bounds else button(key).bounds)

    internal fun bottomScreenBounds(): Rect? = bottomRect?.let { Rect(it) }

    private fun activeControls(): List<Control> =
        if (!controlsVisible) emptyList()
        else buttons + listOfNotNull(dpad, circle.takeIf { !it.bounds.isEmpty }, toggle.takeIf { showToggle })

    private fun controlAt(x: Float, y: Float, slop: Float): Control? =
        activeControls().firstOrNull { it.contains(x, y, slop) }

    private fun inBottom(x: Float, y: Float): Boolean = bottomRect?.contains(x.toInt(), y.toInt()) == true

    private fun onScreenPicture(x: Float, y: Float): Boolean =
        inBottom(x, y) || topRect?.contains(x.toInt(), y.toInt()) == true

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(event: MotionEvent): Boolean {
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val i = event.actionIndex
                pointers.put(event.getPointerId(i), bind(event.getX(i), event.getY(i)))
                for (p in 0 until event.pointerCount) update(event.getPointerId(p), event.getX(p), event.getY(p))
            }
            MotionEvent.ACTION_MOVE ->
                for (p in 0 until event.pointerCount) update(event.getPointerId(p), event.getX(p), event.getY(p))
            MotionEvent.ACTION_POINTER_UP -> release(event.getPointerId(event.actionIndex))
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> releaseAll()
        }
        return true
    }

    private fun bind(x: Float, y: Float): Binding {
        val exact = controlAt(x, y, 1f)
        val control = exact ?: if (inBottom(x, y)) null else controlAt(x, y, HIT_SLOP)
        if (control != null) {
            return when (control.kind) {
                Kind.DPAD -> Binding.Dpad(0)
                Kind.CIRCLE -> Binding.Circle(0, 0)
                Kind.BUTTON -> Binding.Button(control)
                Kind.TOGGLE -> {
                    listener?.onToggleBottomScreen()
                    haptic()
                    Binding.Ignored
                }
            }
        }
        if (inBottom(x, y)) {
            /* The 3DS touch screen is single-touch: the first finger keeps it. */
            for (k in 0 until pointers.size()) if (pointers.valueAt(k) === Binding.Touch) return Binding.Ignored
            return if (InputHub.setTouch(inputSource, true, toBottomX(x), toBottomY(y))) Binding.Touch else Binding.Ignored
        }
        if (hasControls && allowed && autoHidden && !onScreenPicture(x, y)) setAutoHidden(false)
        return Binding.Ignored
    }

    private fun update(id: Int, x: Float, y: Float) {
        when (val b = pointers.get(id) ?: return) {
            is Binding.Touch -> InputHub.setTouch(inputSource, true, toBottomX(x), toBottomY(y))
            is Binding.Dpad -> b.keys = dpadKeys(x, y)
            is Binding.Circle -> {
                val r = circle.bounds.width() / 2f * KNOB_TRAVEL
                var dx = x - circle.bounds.centerX()
                var dy = y - circle.bounds.centerY()
                val len = hypot(dx, dy)
                if (len > r) {
                    dx *= r / len
                    dy *= r / len
                }
                b.x = (dx / r * CtrKeys.CIRCLE_MAX).toInt()
                b.y = (-dy / r * CtrKeys.CIRCLE_MAX).toInt()
            }
            is Binding.Button -> {
                /* A finger may slide from one button to the next. */
                if (b.control?.contains(x, y, HIT_SLOP) != true)
                    b.control = buttons.firstOrNull { it.contains(x, y, 1f) }
            }
            Binding.Ignored -> {}
        }
        commit()
    }

    private fun release(id: Int) {
        if (pointers.get(id) === Binding.Touch) InputHub.setTouch(inputSource, false, 0, 0)
        pointers.remove(id)
        commit()
    }

    private fun releaseControls() {
        for (k in pointers.size() - 1 downTo 0)
            if (pointers.valueAt(k) !== Binding.Touch) pointers.removeAt(k)
        commit()
    }

    /** Drops every pointer, e.g. when the game pauses. */
    fun releaseAll() {
        for (k in 0 until pointers.size())
            if (pointers.valueAt(k) === Binding.Touch) InputHub.setTouch(inputSource, false, 0, 0)
        pointers.clear()
        commit()
    }

    private fun commit() {
        var keys = 0
        var cx = 0
        var cy = 0
        for (k in 0 until pointers.size()) {
            when (val b = pointers.valueAt(k)) {
                is Binding.Button -> keys = keys or (b.control?.key ?: 0)
                is Binding.Dpad -> keys = keys or b.keys
                is Binding.Circle -> {
                    cx = b.x
                    cy = b.y
                }
                else -> {}
            }
        }
        val circleChanged = cx != circleX || cy != circleY
        if (keys == pressedKeys && !circleChanged) return
        if (keys and pressedKeys.inv() != 0) haptic()
        pressedKeys = keys
        circleX = cx
        circleY = cy
        if (hasControls) {
            InputHub.setKeys(inputSource, keys)
            InputHub.setCircle(inputSource, cx, cy)
        }
        invalidate()
    }

    private fun dpadKeys(x: Float, y: Float): Int {
        val r = dpad.bounds.width() / 2f
        val dx = x - dpad.bounds.centerX()
        val dy = y - dpad.bounds.centerY()
        if (hypot(dx, dy) < r * DPAD_DEAD_ZONE) return 0
        val angle = Math.toDegrees(atan2(-dy.toDouble(), dx.toDouble()))
        val sector = Math.floorMod(Math.round(angle / 45.0).toInt(), 8)
        return DPAD_SECTORS[sector]
    }

    private fun toBottomX(x: Float): Int {
        val r = bottomRect ?: return 0
        return ((x - r.left) * CtrKeys.BOTTOM_WIDTH / r.width()).toInt()
    }

    private fun toBottomY(y: Float): Int {
        val r = bottomRect ?: return 0
        return ((y - r.top) * CtrKeys.BOTTOM_HEIGHT / r.height()).toInt()
    }

    private fun haptic() {
        if (settings?.haptics != true) return
        val v = vibrator ?: return
        if (v.hasVibrator()) v.vibrate(VibrationEffect.createPredefined(VibrationEffect.EFFECT_TICK))
    }

    override fun onDraw(canvas: Canvas) {
        if (!controlsVisible) return
        val s = settings ?: return
        val a = s.controlsOpacity
        val u = resources.displayMetrics.density * s.controlsScale
        stroke.strokeWidth = 2f * u
        stroke.color = withAlpha(Color.WHITE, 0.85f * a)
        text.color = withAlpha(Color.WHITE, a)

        drawDpad(canvas, a)
        if (!circle.bounds.isEmpty) drawCircle(canvas, a)
        for (control in buttons) {
            val pressed = pressedKeys and control.key != 0
            fill.color = if (pressed) withAlpha(Color.WHITE, 0.55f * a) else withAlpha(Color.BLACK, 0.45f * a)
            val r = control.bounds
            if (control.round) {
                canvas.drawCircle(r.centerX(), r.centerY(), r.width() / 2f, fill)
                canvas.drawCircle(r.centerX(), r.centerY(), r.width() / 2f, stroke)
                text.textSize = r.height() * 0.42f
            } else {
                val radius = r.height() / 2f
                canvas.drawRoundRect(r, radius, radius, fill)
                canvas.drawRoundRect(r, radius, radius, stroke)
                text.textSize = r.height() * if (control.label.length > 1) 0.4f else 0.55f
            }
            canvas.drawText(control.label, r.centerX(), r.centerY() - (text.ascent() + text.descent()) / 2f, text)
        }
        if (showToggle && !toggle.bounds.isEmpty) {
            val r = toggle.bounds
            fill.color = withAlpha(Color.BLACK, 0.45f * a)
            canvas.drawRoundRect(r, r.height() / 2f, r.height() / 2f, fill)
            canvas.drawRoundRect(r, r.height() / 2f, r.height() / 2f, stroke)
            text.textSize = r.height() * 0.42f
            canvas.drawText(toggle.label, r.centerX(), r.centerY() - (text.ascent() + text.descent()) / 2f, text)
        }
    }

    private fun drawDpad(canvas: Canvas, a: Float) {
        val r = dpad.bounds
        val arm = r.width() * 0.34f
        val cx = r.centerX()
        val cy = r.centerY()
        path.reset()
        path.addRoundRect(RectF(cx - arm / 2f, r.top, cx + arm / 2f, r.bottom), arm * 0.2f, arm * 0.2f, Path.Direction.CW)
        path.addRoundRect(RectF(r.left, cy - arm / 2f, r.right, cy + arm / 2f), arm * 0.2f, arm * 0.2f, Path.Direction.CW)
        path.op(Path(path), Path.Op.UNION)
        fill.color = withAlpha(Color.BLACK, 0.45f * a)
        canvas.drawPath(path, fill)
        fill.color = withAlpha(Color.WHITE, 0.55f * a)
        val half = r.width() / 2f
        canvas.save()
        canvas.clipPath(path)
        if (pressedKeys and CtrKeys.DUP != 0) {
            tmp.set(cx - arm / 2f, r.top, cx + arm / 2f, cy - arm / 2f)
            canvas.drawRect(tmp, fill)
        }
        if (pressedKeys and CtrKeys.DDOWN != 0) {
            tmp.set(cx - arm / 2f, cy + arm / 2f, cx + arm / 2f, r.bottom)
            canvas.drawRect(tmp, fill)
        }
        if (pressedKeys and CtrKeys.DLEFT != 0) {
            tmp.set(r.left, cy - arm / 2f, cx - arm / 2f, cy + arm / 2f)
            canvas.drawRect(tmp, fill)
        }
        if (pressedKeys and CtrKeys.DRIGHT != 0) {
            tmp.set(cx + arm / 2f, cy - arm / 2f, r.right, cy + arm / 2f)
            canvas.drawRect(tmp, fill)
        }
        canvas.restore()
        canvas.drawPath(path, stroke)
        /* Direction marks. */
        fill.color = withAlpha(Color.WHITE, 0.8f * a)
        val t = arm * 0.22f
        val e = half * 0.78f
        for (dir in 0 until 4) {
            canvas.save()
            canvas.rotate(dir * 90f, cx, cy)
            path.reset()
            path.moveTo(cx, cy - e - t)
            path.lineTo(cx - t, cy - e + t * 0.4f)
            path.lineTo(cx + t, cy - e + t * 0.4f)
            path.close()
            canvas.drawPath(path, fill)
            canvas.restore()
        }
    }

    private fun drawCircle(canvas: Canvas, a: Float) {
        val r = circle.bounds
        val radius = r.width() / 2f
        fill.color = withAlpha(Color.BLACK, 0.35f * a)
        canvas.drawCircle(r.centerX(), r.centerY(), radius, fill)
        canvas.drawCircle(r.centerX(), r.centerY(), radius, stroke)
        val travel = radius * KNOB_TRAVEL
        val kx = r.centerX() + circleX * travel / CtrKeys.CIRCLE_MAX
        val ky = r.centerY() - circleY * travel / CtrKeys.CIRCLE_MAX
        fill.color = withAlpha(Color.WHITE, (if (circleX != 0 || circleY != 0) 0.6f else 0.35f) * a)
        canvas.drawCircle(kx, ky, radius * 0.5f, fill)
        canvas.drawCircle(kx, ky, radius * 0.5f, stroke)
    }

    private fun withAlpha(color: Int, alpha: Float): Int =
        Color.argb((alpha.coerceIn(0f, 1f) * 255).toInt(), Color.red(color), Color.green(color), Color.blue(color))

    companion object {
        /* Sizes in dp, before the size setting. */
        private const val MARGIN = 14f
        private const val DPAD_D = 150f
        private const val CIRCLE_D = 120f
        private const val BUTTON_D = 58f
        private const val SHOULDER_W = 88f
        private const val SHOULDER_H = 40f
        private const val SMALL_W = 76f
        private const val SMALL_H = 32f
        private const val TOGGLE_W = 130f
        private const val HIT_SLOP = 1.2f
        private const val DPAD_DEAD_ZONE = 0.22f
        private const val KNOB_TRAVEL = 0.6f

        /* 45-degree sectors counter-clockwise from right; up is +y on screen. */
        private val DPAD_SECTORS = intArrayOf(
            CtrKeys.DRIGHT,
            CtrKeys.DRIGHT or CtrKeys.DUP,
            CtrKeys.DUP,
            CtrKeys.DUP or CtrKeys.DLEFT,
            CtrKeys.DLEFT,
            CtrKeys.DLEFT or CtrKeys.DDOWN,
            CtrKeys.DDOWN,
            CtrKeys.DDOWN or CtrKeys.DRIGHT,
        )
    }
}
