package com.example.monitoreoambiental

import android.graphics.Color
import android.os.Bundle
import android.widget.Button
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import com.google.firebase.auth.FirebaseAuth
import com.google.firebase.database.DataSnapshot
import com.google.firebase.database.DatabaseError
import com.google.firebase.database.DatabaseReference
import com.google.firebase.database.FirebaseDatabase
import com.google.firebase.database.ServerValue
import com.google.firebase.database.ValueEventListener

class  MainActivity : AppCompatActivity() {
    private val database by lazy {
        FirebaseDatabase.getInstance("https://monitoreoambientaliot-dc630-default-rtdb.firebaseio.com")
    }
    private lateinit var readings: DatabaseReference
    private lateinit var actuator: DatabaseReference
    private lateinit var connected: DatabaseReference
    private lateinit var readingsListener: ValueEventListener
    private lateinit var actuatorListener: ValueEventListener
    private lateinit var connectedListener: ValueEventListener
    private lateinit var connectionLabel: TextView
    private lateinit var actuatorLabel: TextView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        if (FirebaseAuth.getInstance().currentUser == null) {
            finish()
            return
        }
        setContentView(R.layout.activity_main)
        connectionLabel = findViewById(R.id.tvConexion)
        actuatorLabel = findViewById(R.id.tvEstadoLed)
        val tempLabel = findViewById<TextView>(R.id.tvTemperatura)
        val humLabel = findViewById<TextView>(R.id.tvHumedad)
        val lightLabel = findViewById<TextView>(R.id.tvLuz)
        val gasLabel = findViewById<TextView>(R.id.tvGas)
        val comfortLabel = findViewById<TextView>(R.id.tvConfort)
        val comfortStateLabel = findViewById<TextView>(R.id.tvEstadoConfort)
        readings = database.reference
        actuator = readings.child("EstadoActuador").child("LED")
        connected = database.getReference(".info/connected")
        readings.keepSynced(true)
        readingsListener = object : ValueEventListener {
            override fun onDataChange(snapshot: DataSnapshot) {

                Toast.makeText(this@MainActivity, "Temperatura recibida: ${snapshot.child("Temperatura").value}", Toast.LENGTH_LONG).show()

                if (!snapshot.exists() || !snapshot.child("Temperatura").exists()) {
                    comfortStateLabel.text = "SIN LECTURAS"
                    return
                }
                val temperature = snapshot.child("Temperatura").value?.toString() ?: "--"
                val humidity = snapshot.child("Humedad").value?.toString() ?: "--"
                val light = snapshot.child("Luz").value?.toString() ?: "--"
                val gas = snapshot.child("Gas").value?.toString() ?: "--"
                val comfort = snapshot.child("Confort").value?.toString()?.toDoubleOrNull()?.toInt()
                tempLabel.text = "$temperature °C"
                humLabel.text = "$humidity %"
                lightLabel.text = light
                gasLabel.text = gas
                comfortLabel.text = if (comfort == null) "-- / 100" else "$comfort / 100"
                if (comfort == null) {
                    comfortStateLabel.text = "ESTADO: SIN DATOS"
                    return
                }
                when {
                    comfort >= 80 -> {
                        comfortStateLabel.text = "ESTADO: ÓPTIMO"
                        comfortStateLabel.setTextColor(Color.parseColor("#10B981"))
                    }
                    comfort >= 50 -> {
                        comfortStateLabel.text = "ESTADO: ACEPTABLE"
                        comfortStateLabel.setTextColor(Color.parseColor("#F59E0B"))
                    }
                    else -> {
                        comfortStateLabel.text = "ESTADO: CRÍTICO"
                        comfortStateLabel.setTextColor(Color.parseColor("#EF4444"))
                    }
                }
            }
            override fun onCancelled(error: DatabaseError) {
                comfortStateLabel.text = "Error: ${error.message}"
            }
        }
        actuatorListener = object : ValueEventListener {
            override fun onDataChange(snapshot: DataSnapshot) {
                val enabled = snapshot.child("encendido").getValue(Boolean::class.java)
                actuatorLabel.text = when (enabled) {
                    true -> "LED: ENCENDIDO (confirmado por ESP32)"
                    false -> "LED: APAGADO (confirmado por ESP32)"
                    null -> "LED: SIN CONFIRMACIÓN"
                }
            }
            override fun onCancelled(error: DatabaseError) {
                actuatorLabel.text = "LED: ${error.message}"
            }
        }
        connectedListener = object : ValueEventListener {
            override fun onDataChange(snapshot: DataSnapshot) {
                val online = snapshot.getValue(Boolean::class.java) == true
                connectionLabel.text = if (online) "Firebase: CONECTADO" else "Firebase: SIN CONEXIÓN"
            }
            override fun onCancelled(error: DatabaseError) {
                connectionLabel.text = "Firebase: ${error.message}"
            }
        }
        readings.addValueEventListener(readingsListener)
        actuator.addValueEventListener(actuatorListener)
        connected.addValueEventListener(connectedListener)
        findViewById<Button>(R.id.btnEncender).setOnClickListener { sendCommand(true) }
        findViewById<Button>(R.id.btnApagar).setOnClickListener { sendCommand(false) }
        findViewById<Button>(R.id.btnSalir).setOnClickListener {
            FirebaseAuth.getInstance().signOut()
            finish()
        }
    }

    private fun sendCommand(enabled: Boolean) {
        val user = FirebaseAuth.getInstance().currentUser ?: return
        actuatorLabel.text = "LED: COMANDO PENDIENTE"
        val command = mapOf(
            "encendido" to enabled,
            "timestamp" to ServerValue.TIMESTAMP,
            "usuario" to user.uid
        )
        database.getReference("Comandos/LED").setValue(command).addOnFailureListener {
            Toast.makeText(this, "No se pudo enviar el comando: ${it.message}", Toast.LENGTH_LONG).show()
        }
    }

    override fun onDestroy() {
        if (::readings.isInitialized) readings.removeEventListener(readingsListener)
        if (::actuator.isInitialized) actuator.removeEventListener(actuatorListener)
        if (::connected.isInitialized) connected.removeEventListener(connectedListener)
        super.onDestroy()
    }
}
