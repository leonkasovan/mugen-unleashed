package org.libsdl.app;

/*
 * TouchControllerOverlay — on-screen virtual gamepad for the mugen-unleashed
 * Android port.
 *
 * HOW IT WORKS
 * ============
 * The overlay is a full-screen RelativeLayout inflated from touch_controller.xml
 * and placed above the SDL surface in SDLActivity's view hierarchy.  It
 * intercepts all touch events before they reach the surface.
 *
 * A virtual joystick is registered via SDLControllerManager.nativeAddJoystick()
 * the first time a touch arrives.  SDL2's Android JNI layer turns every
 * subsequent nativeAddJoystick / onNativePadDown / onNativePadUp /
 * onNativeHat / onNativeJoy call into the same SDL events that a physical
 * gamepad generates.  The engine's Pads_HandleControllerEvent() (pads.c)
 * maps those events through pad_ctrl_map[] to the compact 1-11 button codes
 * the mugen.cfg [P<n> Gamepad] groups reference.
 *
 * BUTTON → ENGINE CODE MAPPING
 * ============================
 *   KEYCODE_BUTTON_A   (96)  → PAD_CTRL_A  (5)   engine: A
 *   KEYCODE_BUTTON_B   (97)  → PAD_CTRL_B  (6)   engine: B
 *   KEYCODE_BUTTON_X   (99)  → PAD_CTRL_X  (8)   engine: X
 *   KEYCODE_BUTTON_Y   (100) → PAD_CTRL_Y  (9)   engine: Y
 *   KEYCODE_BUTTON_L1  (102) → PAD_CTRL_Z  (10)  engine: Z (LB)
 *   KEYCODE_BUTTON_R1  (103) → PAD_CTRL_C  (7)   engine: C (RB)
 *   KEYCODE_BUTTON_START  (108) → PAD_CTRL_START (11)
 *   KEYCODE_BUTTON_SELECT (109) → PAD_CTRL_SELECT → pause
 *   D-pad hat X/Y via onNativeHat → PAD_CTRL_{UP,DOWN,LEFT,RIGHT} (1-4)
 *   Left stick axes 0/1 via onNativeJoy → same 1-4 directions via axis path
 *
 * AUTO-HIDE
 * =========
 * When a physical gamepad is detected the overlay hides itself so it does not
 * consume touch events that the user does not intend for the virtual pad.  It
 * reappears when physical gamepads are disconnected.
 */

import android.content.Context;
import android.hardware.input.InputManager;
import android.os.Build;
import android.view.DisplayCutout;
import android.view.HapticFeedbackConstants;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.LayoutInflater;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.widget.RelativeLayout;

import org.mugen_unleashed.mugen.R;

import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;

public class TouchControllerOverlay extends RelativeLayout {

    // -------------------------------------------------------------------------
    // State
    // -------------------------------------------------------------------------

    /** SDL device ID of the virtual joystick; set during initVirtualDevice(). */
    private int mVirtualDeviceId = -1;

    /** Whether the virtual joystick has been registered with SDL. */
    private boolean mInitialized = false;

    /** Current d-pad hat output (-1, 0, 1 per axis). */
    private int mHatX = 0;
    private int mHatY = 0;

    /**
     * Per-pointer tracking of which button codes each finger currently holds.
     * Key: pointer ID.  Value: set of KEYCODE_BUTTON_* codes.
     */
    private final Map<Integer, Set<Integer>> mPointerButtons = new HashMap<>();

    /** Pointer IDs claimed by the left and right analog sticks (-1 = none). */
    private int mLeftStickPointerId  = -1;

    /** Registered while attached, so a pad plug/unplug re-evaluates visibility. */
    private InputManager mInputManager;
    private InputManager.InputDeviceListener mDeviceListener;

    // -------------------------------------------------------------------------
    // Construction
    // -------------------------------------------------------------------------

    public TouchControllerOverlay(Context context) {
        super(context);
        setClickable(true);
        setFocusable(true);
        setEnabled(true);
        setMotionEventSplittingEnabled(true);

        LayoutInflater.from(context).inflate(R.layout.touch_controller, this, true);

        /*
         * Keep the controls out of the display cutout and away from the system
         * bars.
         *
         * The overlay is laid out over the whole panel, but a device with a
         * notch only shows part of it: on a P50 Pro the window is 2700 px wide
         * while 2464 px are usable, so a button anchored to the right edge with
         * alignParentRight landed under the cutout and was clipped to a single
         * pixel -- visible in the hierarchy dump, invisible and unhittable on
         * the screen. Applying the insets as padding moves the anchor to the
         * usable edge. Devices without a cutout report zero and nothing moves.
         */
        setOnApplyWindowInsetsListener(new OnApplyWindowInsetsListener() {
            @Override
            public WindowInsets onApplyWindowInsets(View v, WindowInsets insets) {
                int left = insets.getSystemWindowInsetLeft();
                int right = insets.getSystemWindowInsetRight();
                int bottom = insets.getSystemWindowInsetBottom();
                int top = insets.getSystemWindowInsetTop();

                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
                    DisplayCutout cut = insets.getDisplayCutout();
                    if (cut != null) {
                        left = Math.max(left, cut.getSafeInsetLeft());
                        right = Math.max(right, cut.getSafeInsetRight());
                        bottom = Math.max(bottom, cut.getSafeInsetBottom());
                        top = Math.max(top, cut.getSafeInsetTop());
                    }
                }

                if (getPaddingLeft() != left || getPaddingRight() != right ||
                    getPaddingTop() != top || getPaddingBottom() != bottom) {
                    setPadding(left, top, right, bottom);
                }
                return insets;
            }
        });

        // Child buttons must not independently steal touch focus; the overlay
        // routes everything through its own onTouchEvent.
        disableChildTouches(this);
    }

    /** Recursively remove clickable/focusable from every child. */
    private void disableChildTouches(View v) {
        v.setClickable(false);
        v.setFocusable(false);
        if (v instanceof ViewGroup) {
            ViewGroup vg = (ViewGroup) v;
            for (int i = 0; i < vg.getChildCount(); i++) {
                disableChildTouches(vg.getChildAt(i));
            }
        }
    }

    // -------------------------------------------------------------------------
    // Physical gamepad detection → auto-hide
    // -------------------------------------------------------------------------

    /**
     * Returns the number of physical gamepads currently connected.
     * Sensors and other non-pad devices that report SOURCE_JOYSTICK are
     * filtered out by requiring at least one actual button (the same guard
     * ikemen-droid-pro uses).
     */
    private int countPhysicalGamepads() {
        int count = 0;
        for (int id : InputDevice.getDeviceIds()) {
            InputDevice dev = InputDevice.getDevice(id);
            if (isRealGamepad(dev)) count++;
        }
        return count;
    }

    private boolean isRealGamepad(InputDevice device) {
        if (device == null || device.isVirtual()) return false;
        int src = device.getSources();
        boolean isJoy = ((src & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK)
                     || ((src & InputDevice.SOURCE_GAMEPAD)  == InputDevice.SOURCE_GAMEPAD);
        if (!isJoy) return false;

        // Ghost filter: sensors report SOURCE_JOYSTICK but have no buttons.
        boolean[] has = device.hasKeys(
                KeyEvent.KEYCODE_BUTTON_A,
                KeyEvent.KEYCODE_BUTTON_B,
                KeyEvent.KEYCODE_BUTTON_X,
                KeyEvent.KEYCODE_BUTTON_Y,
                KeyEvent.KEYCODE_BUTTON_START,
                KeyEvent.KEYCODE_BUTTON_SELECT);
        for (boolean b : has) { if (b) return true; }
        return false;
    }

    /**
     * Re-evaluate visibility based on physical gamepad presence.
     * Call this whenever a gamepad connection change is detected.
     */
    public void updateVisibility() {
        boolean hide = countPhysicalGamepads() > 0;
        if (hide) {
            /*
             * Release everything before going invisible. The overlay stops
             * receiving touches once hidden, so a button held at the moment a
             * pad is plugged in would otherwise stay down forever -- the engine
             * would see a permanently pressed attack button.
             */
            releaseAll();
        }
        setVisibility(hide ? View.INVISIBLE : View.VISIBLE);
    }

    /** Releases every held button, the d-pad and the analog stick. */
    private void releaseAll() {
        for (Integer pointerId : new HashSet<>(mPointerButtons.keySet())) {
            releasePointerButtons(pointerId);
        }
        mPointerButtons.clear();

        AnalogStickView leftStick = findViewById(R.id.vctrl_left_stick);
        if (leftStick != null && mLeftStickPointerId != -1) {
            resetJoystick(leftStick);
        }
        mLeftStickPointerId = -1;

        if (mHatX != 0 || mHatY != 0) {
            mHatX = 0;
            mHatY = 0;
            if (mVirtualDeviceId >= 0) {
                SDLControllerManager.onNativeHat(mVirtualDeviceId, 0, 0, 0);
            }
            updateDpadVisual(0, 0);
        }
    }

    // -------------------------------------------------------------------------
    // Attachment: watch for physical pads so auto-hide reacts to plug/unplug
    // -------------------------------------------------------------------------

    @Override
    protected void onAttachedToWindow() {
        super.onAttachedToWindow();
        requestApplyInsets();
        if (mDeviceListener == null) {
            mInputManager = (InputManager) getContext().getSystemService(Context.INPUT_SERVICE);
            if (mInputManager != null) {
                mDeviceListener = new InputManager.InputDeviceListener() {
                    @Override
                    public void onInputDeviceAdded(int deviceId) {
                        updateVisibility();
                    }

                    @Override
                    public void onInputDeviceRemoved(int deviceId) {
                        updateVisibility();
                    }

                    @Override
                    public void onInputDeviceChanged(int deviceId) {
                        updateVisibility();
                    }
                };
                mInputManager.registerInputDeviceListener(mDeviceListener, null);
            }
        }
        updateVisibility();
    }

    @Override
    protected void onDetachedFromWindow() {
        if (mInputManager != null && mDeviceListener != null) {
            mInputManager.unregisterInputDeviceListener(mDeviceListener);
            mDeviceListener = null;
            mInputManager = null;
        }
        /* Do not leave the virtual pad holding a button after the view is gone. */
        releaseAll();
        super.onDetachedFromWindow();
    }

    // -------------------------------------------------------------------------
    // Virtual device lifecycle
    // -------------------------------------------------------------------------

    /**
     * Register the virtual joystick with SDL on the first touch.
     * We defer this to the first touch so the physical joystick list is stable.
     */
    /**
     * An Android input-device ID no real device is using.
     *
     * SDL's Android joystick layer keys everything by the Android device ID, so
     * the virtual pad must not reuse one: a count of connected pads is not an
     * ID (with no pads that would be 0, which the touchscreen already holds).
     * Take the highest ID in use and step past it.
     */
    private int nextFreeDeviceId() {
        int maxId = -1;
        for (int id : InputDevice.getDeviceIds()) {
            if (id > maxId) maxId = id;
        }
        return maxId + 1;
    }

    private void initVirtualDevice() {
        // Choose an ID no current device holds so SDL does not confuse the
        // virtual device with a real one.
        mVirtualDeviceId = nextFreeDeviceId();

        // Remove any stale registration from a previous session.
        SDLControllerManager.nativeRemoveJoystick(mVirtualDeviceId);

        // Register as an Xbox 360-style controller (same identity ikemen-droid-pro
        // uses, well-covered by SDL's gamepad database).
        SDLControllerManager.nativeAddJoystick(
                mVirtualDeviceId,
                "Virtual Controller",   // name
                "Gamepad",              // desc
                0x045E,                 // vendor  (Microsoft)
                0x028E,                 // product (Xbox 360)
                false,                  // is_accelerometer
                0xFFF,                  // button_mask (12 buttons)
                6,                      // naxes
                0x3F,                   // axis_mask
                1,                      // nhats
                0);                     // nballs

        // Warm up all axes so SDL allocates its internal state before the
        // first real event arrives (a tiny non-zero then zero flushes it).
        for (int axis = 0; axis < 6; axis++) {
            SDLControllerManager.onNativeJoy(mVirtualDeviceId, axis, 0.00390625f);
            SDLControllerManager.onNativeJoy(mVirtualDeviceId, axis, 0.0f);
        }
        SDLControllerManager.onNativeHat(mVirtualDeviceId, 0, 0, 0);

        // Wire up the analog stick view with the virtual device ID.
        AnalogStickView leftStick = findViewById(R.id.vctrl_left_stick);
        if (leftStick != null) {
            leftStick.setAttrs(mVirtualDeviceId, 0, 1);
        }

        mInitialized = true;
    }

    // -------------------------------------------------------------------------
    // Touch event routing
    // -------------------------------------------------------------------------

    @Override
    public boolean onInterceptTouchEvent(MotionEvent ev) {
        // Lazy-init on first touch so physical pad detection is stable.
        if (!mInitialized) {
            initVirtualDevice();
        }
        // Absorb all events so the SDL surface does not see them as mouse/touch.
        return true;
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        int action = event.getActionMasked();
        int idx    = event.getActionIndex();
        int pId    = event.getPointerId(idx);
        float x    = event.getX(idx);
        float y    = event.getY(idx);

        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN:
                handlePointerDown(pId, x, y);
                break;

            case MotionEvent.ACTION_MOVE:
                for (int i = 0; i < event.getPointerCount(); i++) {
                    int movePId = event.getPointerId(i);
                    float mx = event.getX(i);
                    float my = event.getY(i);
                    handlePointerMove(movePId, mx, my);
                }
                break;

            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP:
            case MotionEvent.ACTION_CANCEL:
                handlePointerUp(pId);
                break;
        }

        // D-pad hat is recalculated on every event (all pointers, any action).
        updateDpadHat(event);
        return true;
    }

    private void handlePointerDown(int pId, float x, float y) {
        AnalogStickView leftStick = findViewById(R.id.vctrl_left_stick);
        if (leftStick != null && isViewAtLocation(leftStick, x, y)) {
            mLeftStickPointerId = pId;
            updateJoystick(leftStick, x, y);
        } else {
            updatePointerButtons(pId, buttonsAt(x, y));
        }
    }

    private void handlePointerMove(int movePId, float mx, float my) {
        if (movePId == mLeftStickPointerId) {
            AnalogStickView leftStick = findViewById(R.id.vctrl_left_stick);
            if (leftStick != null) updateJoystick(leftStick, mx, my);
        } else {
            Set<Integer> cur  = buttonsAt(mx, my);
            Set<Integer> last = mPointerButtons.get(movePId);
            if (last == null || !cur.equals(last)) {
                updatePointerButtons(movePId, cur);
            }
        }
    }

    private void handlePointerUp(int pId) {
        if (pId == mLeftStickPointerId) {
            AnalogStickView leftStick = findViewById(R.id.vctrl_left_stick);
            if (leftStick != null) resetJoystick(leftStick);
            mLeftStickPointerId = -1;
        } else {
            releasePointerButtons(pId);
        }
    }

    // -------------------------------------------------------------------------
    // Analog stick helpers
    // -------------------------------------------------------------------------

    private void updateJoystick(AnalogStickView joy, float touchX, float touchY) {
        float centerX = joy.getLeft() + (joy.getWidth()  / 2f);
        float centerY = joy.getTop()  + (joy.getHeight() / 2f);
        float radius  = joy.getWidth() / 3f;

        float dx = touchX - centerX;
        float dy = touchY - centerY;
        float dist = (float) Math.sqrt(dx * dx + dy * dy);

        // Clamp to the stick radius.
        if (dist > radius) {
            dx = (dx / dist) * radius;
            dy = (dy / dist) * radius;
        }

        joy.updateVisualPos((joy.getWidth() / 2f) + dx, (joy.getHeight() / 2f) + dy);
        joy.sendToSDL(dx / radius, dy / radius);
    }

    private void resetJoystick(AnalogStickView joy) {
        joy.updateVisualPos(joy.getWidth() / 2f, joy.getHeight() / 2f);
        joy.sendToSDL(0f, 0f);
    }

    // -------------------------------------------------------------------------
    // Button press / release bookkeeping
    // -------------------------------------------------------------------------

    /**
     * Diff the old and new button sets for this pointer, sending press/release
     * events only for the buttons that changed.
     */
    private void updatePointerButtons(int pointerId, Set<Integer> newCodes) {
        Set<Integer> oldCodes = mPointerButtons.get(pointerId);
        if (oldCodes == null) oldCodes = new HashSet<>();

        for (Integer code : oldCodes) {
            if (!newCodes.contains(code)) dispatchButton(code, false);
        }
        for (Integer code : newCodes) {
            if (!oldCodes.contains(code)) dispatchButton(code, true);
        }

        mPointerButtons.put(pointerId, newCodes);
    }

    private void releasePointerButtons(int pointerId) {
        Set<Integer> codes = mPointerButtons.remove(pointerId);
        if (codes == null) return;
        for (Integer code : codes) dispatchButton(code, false);
    }

    // -------------------------------------------------------------------------
    // SDL input dispatch
    // -------------------------------------------------------------------------

    private void dispatchButton(int code, boolean pressed) {
        if (mVirtualDeviceId < 0) return;

        if (pressed) {
            SDLControllerManager.onNativePadDown(mVirtualDeviceId, code);
            // Lightweight haptic feedback on press only.
            performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY,
                    HapticFeedbackConstants.FLAG_IGNORE_GLOBAL_SETTING);
        } else {
            SDLControllerManager.onNativePadUp(mVirtualDeviceId, code);
        }

        updateButtonVisual(code, pressed);
    }

    // -------------------------------------------------------------------------
    // D-pad hat computation (angle-based 8-way, wide diagonals)
    // -------------------------------------------------------------------------

    private void updateDpadHat(MotionEvent event) {
        int action = event.getActionMasked();
        int newX = 0, newY = 0;

        if (action != MotionEvent.ACTION_UP && action != MotionEvent.ACTION_CANCEL) {
            View dpad = findViewById(R.id.vctrl_dpad);
            if (dpad == null) return;

            float centerX = dpad.getLeft() + (dpad.getWidth()  / 2f);
            float centerY = dpad.getTop()  + (dpad.getHeight() / 2f);
            float radius  = dpad.getWidth() / 2f;

            for (int i = 0; i < event.getPointerCount(); i++) {
                // Skip the pointer being lifted on POINTER_UP.
                if (action == MotionEvent.ACTION_POINTER_UP
                        && i == event.getActionIndex()) continue;

                // Skip pointers claimed by the analog stick.
                if (event.getPointerId(i) == mLeftStickPointerId) continue;

                float dx = event.getX(i) - centerX;
                float dy = event.getY(i) - centerY;
                float dist = (float) Math.sqrt(dx * dx + dy * dy);

                // Dead-zone (20% centre) and outer bound (150% radius).
                if (dist > (radius * 0.2f) && dist < (radius * 1.5f)) {
                    double angle = Math.toDegrees(Math.atan2(dy, dx));
                    if (angle < 0) angle += 360;

                    // Cardinals get 30°, diagonals 60°.
                    if      (angle >= 345 || angle < 15)   { newX =  1; }
                    else if (angle >=  15 && angle < 75)   { newX =  1; newY =  1; }
                    else if (angle >=  75 && angle < 105)  {             newY =  1; }
                    else if (angle >= 105 && angle < 165)  { newX = -1; newY =  1; }
                    else if (angle >= 165 && angle < 195)  { newX = -1; }
                    else if (angle >= 195 && angle < 255)  { newX = -1; newY = -1; }
                    else if (angle >= 255 && angle < 285)  {             newY = -1; }
                    else if (angle >= 285 && angle < 345)  { newX =  1; newY = -1; }
                    break; // One finger suffices for the d-pad.
                }
            }
        }

        if (newX != mHatX || newY != mHatY) {
            mHatX = newX;
            mHatY = newY;
            if (mVirtualDeviceId >= 0) {
                SDLControllerManager.onNativeHat(mVirtualDeviceId, 0, mHatX, mHatY);
            }
            updateDpadVisual(mHatX, mHatY);
        }
    }

    // -------------------------------------------------------------------------
    // Hit detection
    // -------------------------------------------------------------------------

    /**
     * Returns the button code whose view the point hits, or an empty set.
     *
     * At most one button is returned. The 16 dp slop makes small buttons easier
     * to hit, but it also makes neighbours overlap -- and Start/Select sit 5 dp
     * apart, so one tap used to press both and pause the match. When several
     * views contain the point, the one whose centre is nearest wins.
     */
    private Set<Integer> buttonsAt(float x, float y) {
        return pickNearest(x, y, dpToPx(16));
    }

    private Set<Integer> pickNearest(float x, float y, int pad) {
        final int[] ids = {
                R.id.vctrl_btn_a, R.id.vctrl_btn_b, R.id.vctrl_btn_x, R.id.vctrl_btn_y,
                R.id.vctrl_btn_z, R.id.vctrl_btn_c, R.id.vctrl_btn_start, R.id.vctrl_btn_select
        };
        final int[] codes = {
                KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_BUTTON_B,
                KeyEvent.KEYCODE_BUTTON_X, KeyEvent.KEYCODE_BUTTON_Y,
                KeyEvent.KEYCODE_BUTTON_L1, KeyEvent.KEYCODE_BUTTON_R1,
                KeyEvent.KEYCODE_BUTTON_START, KeyEvent.KEYCODE_BUTTON_SELECT
        };

        Set<Integer> result = new HashSet<>();
        int bestCode = -1;
        float bestDist = Float.MAX_VALUE;

        for (int i = 0; i < ids.length; i++) {
            View v = findViewById(ids[i]);
            if (v == null) continue;

            int[] vLoc = new int[2];
            int[] pLoc = new int[2];
            v.getLocationOnScreen(vLoc);
            this.getLocationOnScreen(pLoc);

            float cx = (vLoc[0] - pLoc[0]) + v.getWidth()  / 2f;
            float cy = (vLoc[1] - pLoc[1]) + v.getHeight() / 2f;

            /* Inside the view, or within the slop of its edge. */
            float halfW = v.getWidth()  / 2f + pad;
            float halfH = v.getHeight() / 2f + pad;
            if (Math.abs(x - cx) > halfW || Math.abs(y - cy) > halfH) continue;

            float dx = x - cx, dy = y - cy;
            float dist = dx * dx + dy * dy;
            if (dist < bestDist) {
                bestDist = dist;
                bestCode = codes[i];
            }
        }

        if (bestCode != -1) result.add(bestCode);
        return result;
    }

    /** True if the given view's area contains the overlay-local point (x, y). */
    private boolean isViewAtLocation(View v, float x, float y) {
        if (v == null || v.getVisibility() != View.VISIBLE) return false;
        int[] vLoc = new int[2], pLoc = new int[2];
        v.getLocationOnScreen(vLoc);
        this.getLocationOnScreen(pLoc);
        int relLeft = vLoc[0] - pLoc[0];
        int relTop  = vLoc[1] - pLoc[1];
        return x >= relLeft && x <= (relLeft + v.getWidth())
            && y >= relTop  && y <= (relTop  + v.getHeight());
    }

    // -------------------------------------------------------------------------
    // Visual feedback
    // -------------------------------------------------------------------------

    private void updateButtonVisual(int code, boolean pressed) {
        int viewId = -1;
        if      (code == KeyEvent.KEYCODE_BUTTON_A)      viewId = R.id.vctrl_btn_a;
        else if (code == KeyEvent.KEYCODE_BUTTON_B)      viewId = R.id.vctrl_btn_b;
        else if (code == KeyEvent.KEYCODE_BUTTON_X)      viewId = R.id.vctrl_btn_x;
        else if (code == KeyEvent.KEYCODE_BUTTON_Y)      viewId = R.id.vctrl_btn_y;
        else if (code == KeyEvent.KEYCODE_BUTTON_L1)     viewId = R.id.vctrl_btn_z;
        else if (code == KeyEvent.KEYCODE_BUTTON_R1)     viewId = R.id.vctrl_btn_c;
        else if (code == KeyEvent.KEYCODE_BUTTON_START)  viewId = R.id.vctrl_btn_start;
        else if (code == KeyEvent.KEYCODE_BUTTON_SELECT) viewId = R.id.vctrl_btn_select;

        if (viewId != -1) {
            View v = findViewById(viewId);
            if (v != null) v.setPressed(pressed);
        }
    }

    private void updateDpadVisual(int hX, int hY) {
        setViewPressed(R.id.vctrl_dp_up,    hY == -1);
        setViewPressed(R.id.vctrl_dp_down,  hY ==  1);
        setViewPressed(R.id.vctrl_dp_left,  hX == -1);
        setViewPressed(R.id.vctrl_dp_right, hX ==  1);
    }

    private void setViewPressed(int id, boolean pressed) {
        View v = findViewById(id);
        if (v != null) v.setPressed(pressed);
    }

    // -------------------------------------------------------------------------
    // Utilities
    // -------------------------------------------------------------------------

    private int dpToPx(int dp) {
        float density = getContext().getResources().getDisplayMetrics().density;
        return Math.round(dp * density);
    }
}
