/* Emulator QA helper: inject a real finger tap, including Android 11 DocumentsUI.
 * adb shell input tap on API 30 reports TOOL_TYPE_UNKNOWN, which the file
 * picker selection handler can ignore. See docs/BUILDING.md for build/use.
 * Runs as adb shell via app_process; never packaged into the game APK.
 */
import android.hardware.input.InputManager;
import android.view.InputDevice;
import android.view.InputEvent;
import android.view.MotionEvent;
import android.os.SystemClock;
import java.lang.reflect.Method;
public class AndroidTouch {
  public static void main(String[] args) throws Exception {
    if (args.length < 2 || args.length > 3) {
      System.err.println("Usage: AndroidTouch x y [displayId]");
      System.exit(2);
    }
    Object manager = InputManager.class.getDeclaredMethod("getInstance").invoke(null);
    Method inject = InputManager.class.getDeclaredMethod("injectInputEvent", InputEvent.class, int.class);
    float x = Float.parseFloat(args[0]), y = Float.parseFloat(args[1]);
    MotionEvent.PointerProperties pp = new MotionEvent.PointerProperties();
    pp.id = 0; pp.toolType = MotionEvent.TOOL_TYPE_FINGER;
    MotionEvent.PointerCoords pc = new MotionEvent.PointerCoords();
    pc.x = x; pc.y = y; pc.pressure = 1; pc.size = 1;
    long down = SystemClock.uptimeMillis();
    for (int action : new int[] {MotionEvent.ACTION_DOWN, MotionEvent.ACTION_UP}) {
      MotionEvent e = MotionEvent.obtain(down, SystemClock.uptimeMillis(), action, 1,
        new MotionEvent.PointerProperties[]{pp}, new MotionEvent.PointerCoords[]{pc},
        0, 0, 1, 1, 0, 0, InputDevice.SOURCE_TOUCHSCREEN, 0);
      InputEvent.class.getDeclaredMethod("setDisplayId", int.class).invoke(e, args.length == 3 ? Integer.parseInt(args[2]) : 0);
      System.out.println("finger action=" + action + " injected=" + inject.invoke(manager, e, 2));
      e.recycle();
      SystemClock.sleep(80);
    }
  }
}
