package org.libsdl.app;

/*
 * AnalogStickView — on-screen analog stick widget for the mugen-unleashed
 * virtual controller overlay.
 *
 * Draws a translucent base circle with a movable knob.  The parent
 * (TouchControllerOverlay) owns all touch logic and calls:
 *   updateVisualPos(x, y)  — reposition the knob in view-local coords
 *   sendToSDL(normX, normY) — push [-1..1] axis values to the SDL JNI layer
 *
 * axisX / axisY are read from XML via the AnalogStickView declare-styleable.
 * They default to axis 0/1 (left stick X/Y).
 */

import android.content.Context;
import android.content.res.TypedArray;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.util.AttributeSet;
import android.view.View;

import org.mugen_unleashed.mugen.R;

public class AnalogStickView extends View {

    private int mDeviceId = 0;
    private int mAxisX   = 0;
    private int mAxisY   = 1;

    private float mCenterX, mCenterY, mRadius;
    private float mKnobX,   mKnobY;

    // -------------------------------------------------------------------------
    // Construction
    // -------------------------------------------------------------------------

    public AnalogStickView(Context context) {
        super(context);
    }

    /** Used when the parent creates the view programmatically with explicit IDs. */
    public AnalogStickView(Context context, int deviceId, int axisX, int axisY) {
        super(context);
        mDeviceId = deviceId;
        mAxisX    = axisX;
        mAxisY    = axisY;
    }

    public AnalogStickView(Context context, AttributeSet attrs) {
        super(context, attrs);
        initFromAttrs(context, attrs);
    }

    public AnalogStickView(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
        initFromAttrs(context, attrs);
    }

    private void initFromAttrs(Context context, AttributeSet attrs) {
        if (attrs == null) return;
        TypedArray a = context.obtainStyledAttributes(attrs, R.styleable.AnalogStickView);
        try {
            mAxisX = a.getInt(R.styleable.AnalogStickView_axisX, 0);
            mAxisY = a.getInt(R.styleable.AnalogStickView_axisY, 1);
        } finally {
            a.recycle();
        }
    }

    // -------------------------------------------------------------------------
    // Public API
    // -------------------------------------------------------------------------

    /** Called by the parent after the virtual device is registered. */
    public void setAttrs(int deviceId, int axisX, int axisY) {
        mDeviceId = deviceId;
        mAxisX    = axisX;
        mAxisY    = axisY;
    }

    /** Reposition the knob visual; x/y are view-local pixels. */
    public void updateVisualPos(float x, float y) {
        mKnobX = x;
        mKnobY = y;
        invalidate();
    }

    /**
     * Push normalised [-1..1] axis values to SDL.
     * No-op in the layout editor (isInEditMode).
     */
    public void sendToSDL(float normX, float normY) {
        if (!isInEditMode()) {
            SDLControllerManager.onNativeJoy(mDeviceId, mAxisX, normX);
            SDLControllerManager.onNativeJoy(mDeviceId, mAxisY, normY);
        }
    }

    // -------------------------------------------------------------------------
    // View overrides
    // -------------------------------------------------------------------------

    @Override
    protected void onMeasure(int widthSpec, int heightSpec) {
        int defSize = 200;
        int w = resolveSize(defSize, widthSpec);
        int h = resolveSize(defSize, heightSpec);
        setMeasuredDimension(w, h);
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldW, int oldH) {
        mCenterX = w / 2f;
        mCenterY = h / 2f;
        mRadius  = Math.min(w, h) / 3.0f;
        mKnobX   = mCenterX;
        mKnobY   = mCenterY;
    }

    @Override
    protected void onDraw(Canvas canvas) {
        // Recompute if onSizeChanged never fired (e.g. layout editor).
        if (mRadius <= 0) {
            float w = getWidth(), h = getHeight();
            if (w <= 0 || h <= 0) return;
            mCenterX = w / 2f;
            mCenterY = h / 2f;
            mRadius  = Math.min(w, h) / 3.0f;
            mKnobX   = mCenterX;
            mKnobY   = mCenterY;
        }

        Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);

        // Base ring
        p.setColor(0x44FFFFFF);
        canvas.drawCircle(mCenterX, mCenterY, mRadius, p);

        // Knob
        p.setColor(0x88FFFFFF);
        canvas.drawCircle(mKnobX, mKnobY, mRadius / 2.0f, p);
    }
}
