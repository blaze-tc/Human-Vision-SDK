package com.blazetc.humanvision.surfaceab;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.util.Log;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

public final class ProbeActivity extends Activity implements SurfaceHolder.Callback {
    private static final String TAG = "HVSurfaceAB";
    private static final String DEFAULT_URL = "rtsp://127.0.0.1:18566/Exercise-11";
    private static final String DEFAULT_CODEC = "OMX.hisi.video.decoder.avc";
    private static final String DEFAULT_VARIANTS = "private,surface";

    private boolean started;

    private static native int run(String url, Surface surface, String codecName, int width,
                                  int height, int packets, String variants);

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        SurfaceView view = new SurfaceView(this);
        view.getHolder().addCallback(this);
        setContentView(view);
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        Surface surface = holder.getSurface();
        if (started || surface == null || !surface.isValid()) {
            return;
        }
        started = true;

        Intent options = getIntent();
        String url = stringOption(options, "url", DEFAULT_URL);
        String codec = stringOption(options, "codec", DEFAULT_CODEC);
        int width = options.getIntExtra("width", 1920);
        int height = options.getIntExtra("height", 1080);
        int packets = options.getIntExtra("packets", 90);
        String variants = stringOption(options, "variants", DEFAULT_VARIANTS);
        Thread worker = new Thread(() -> {
            try {
                System.loadLibrary("hv_surface_ab");
                if (!surface.isValid()) {
                    Log.e(TAG, "probe_aborted=surface_invalid");
                    return;
                }
                Log.i(TAG, "probe_started codec=" + codec + " size=" + width + "x" + height
                        + " packets=" + packets + " variants=" + variants);
                int result = run(url, surface, codec, width, height, packets, variants);
                Log.i(TAG, "probe_result=" + result);
            } catch (LinkageError | RuntimeException failure) {
                Log.e(TAG, "probe_error", failure);
            }
        }, "HVSurfaceAB-worker");
        worker.start();
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        Log.i(TAG, "surface_destroyed");
    }

    private static String stringOption(Intent options, String key, String fallback) {
        String value = options.getStringExtra(key);
        return value == null || value.isEmpty() ? fallback : value;
    }
}
