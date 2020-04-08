package com.zhuotong.myihk;

import android.os.Bundle;
import android.util.Log;
import android.view.View;

import androidx.appcompat.app.AppCompatActivity;

public class MainActivity extends AppCompatActivity {

    static {
        try {
            System.loadLibrary("services");
        } catch (Throwable e) {
            e.printStackTrace();
        }
    }

    public native String[] invokeNativeMethod();

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        findViewById(R.id.test_button).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                String  string = Utils.getSystemProperites("ro.build.fingerprint");
                Log.d("Test", "ro.build.fingerprint: " + string);

                String string1 = Utils.getInterfacesHDAddresses().toString();
                Log.d("Test", "getWlan0HDAddress: " + string1);

                String[] strings = invokeNativeMethod();
                Log.d("Test", "invokeNativeMethod: " + strings.toString());
            }
        });
    }
}
