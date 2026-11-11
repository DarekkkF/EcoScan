package com.example.monitoreoambiental

import android.app.Application
import com.google.firebase.database.FirebaseDatabase

class MonitoreoApplication : Application() {
    override fun onCreate() {
        super.onCreate()
        FirebaseDatabase.getInstance("https://monitoreoambientaliot-dc630-default-rtdb.firebaseio.com").setPersistenceEnabled(true)
    }
}
