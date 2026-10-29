package com.lefteros.vaultink

import android.content.Context
import android.net.nsd.NsdManager
import android.net.nsd.NsdServiceInfo
import android.net.wifi.WifiManager
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withTimeoutOrNull
import kotlin.coroutines.resume

/**
 * Finds the device by its mDNS service (_http._tcp named "vaultink"), the way it announces itself
 * while File Transfer is open. Returns "host:port", or null after a few seconds.
 */
suspend fun discoverDevice(context: Context, timeoutMs: Long = 6000): String? {
    val nsd = context.getSystemService(Context.NSD_SERVICE) as NsdManager
    val wifi = context.applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager
    val lock = wifi.createMulticastLock("vaultink-discovery").apply { setReferenceCounted(false); acquire() }
    try {
        return withTimeoutOrNull(timeoutMs) {
            suspendCancellableCoroutine { cont ->
                // NsdManager takes one resolve at a time (a second fails with FAILURE_ALREADY_ACTIVE), so
                // services found meanwhile wait their turn; a failed resolve or one with no address moves on.
                val waiting = ArrayDeque<NsdServiceInfo>()
                var resolving = false
                var listener: NsdManager.DiscoveryListener? = null
                fun next() {
                    synchronized(waiting) {
                        if (resolving || !cont.isActive) return
                        val info = waiting.removeFirstOrNull() ?: return
                        resolving = true
                        @Suppress("DEPRECATION")
                        nsd.resolveService(info, object : NsdManager.ResolveListener {
                            override fun onResolveFailed(info: NsdServiceInfo, error: Int) {
                                synchronized(waiting) { resolving = false }
                                next()
                            }
                            override fun onServiceResolved(resolved: NsdServiceInfo) {
                                synchronized(waiting) { resolving = false }
                                @Suppress("DEPRECATION")
                                val host = resolved.host?.hostAddress
                                if (host == null) { next(); return }
                                runCatching { nsd.stopServiceDiscovery(listener) }
                                if (cont.isActive) cont.resume("$host:${resolved.port}")
                            }
                        })
                    }
                }
                listener = object : NsdManager.DiscoveryListener {
                    override fun onServiceFound(info: NsdServiceInfo) {
                        if (!info.serviceName.startsWith("vaultink", ignoreCase = true)) return
                        synchronized(waiting) { waiting.addLast(info) }
                        next()
                    }
                    override fun onDiscoveryStarted(serviceType: String) {}
                    override fun onDiscoveryStopped(serviceType: String) {}
                    override fun onServiceLost(info: NsdServiceInfo) {}
                    override fun onStartDiscoveryFailed(serviceType: String, errorCode: Int) {
                        if (cont.isActive) cont.resume(null)
                    }
                    override fun onStopDiscoveryFailed(serviceType: String, errorCode: Int) {}
                }
                nsd.discoverServices("_http._tcp", NsdManager.PROTOCOL_DNS_SD, listener)
                cont.invokeOnCancellation { runCatching { nsd.stopServiceDiscovery(listener) } }
            }
        }
    } finally {
        lock.release()
    }
}
