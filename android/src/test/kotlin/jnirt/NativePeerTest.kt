package jnirt

import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Before
import org.junit.Test
import java.util.concurrent.CountDownLatch
import java.util.concurrent.atomic.AtomicInteger
import kotlin.concurrent.thread

/**
 * [NativePeer] without a native library: closing a handle and the leak guard are replaced by fakes (PeerRuntime).
 * The real close is the native side, tested on the host in tests/host/runtime/peer_convert_test.cpp.
 */
class NativePeerTest {

    private class Camera(handle: Long) : NativePeer(handle) {
        fun read(): Long = call { it }
    }

    private class RecordingGuard : LeakGuard {
        val watched = mutableListOf<Pair<Any, PeerState>>()
        override fun watch(owner: Any, state: PeerState) {
            watched += owner to state
        }
    }

    private val closed = mutableListOf<Long>()
    private val guard = RecordingGuard()

    @Before
    fun fakeTheNativeSide() {
        PeerRuntime.release = { synchronized(closed) { closed += it } }
        PeerRuntime.guard = guard
    }

    @After
    fun putTheRealOnesBack() {
        PeerRuntime.release = { NativePeerNatives.close(it) }
        PeerRuntime.guard = LeakGuards.default()
        LeakGuards.warnOnLeak = false
    }

    @Test
    fun aCallGetsTheHandle() {
        assertEquals(7L, Camera(7).read())
    }

    @Test
    fun closeReleasesTheHandleOnce() {
        val camera = Camera(7)
        camera.close()
        camera.close()
        assertEquals(listOf(7L), closed)
        assertTrue(camera.isClosed)
    }

    @Test
    fun aCallAfterCloseIsAnIllegalStateException() {
        val camera = Camera(7)
        camera.close()
        try {
            camera.read()
            fail("no exception")
        } catch (e: IllegalStateException) {
            assertTrue(e.message!!.contains("Camera is closed"))
        }
    }

    @Test
    fun useClosesAtTheEnd() {
        Camera(3).use { assertEquals(3L, it.read()) }
        assertEquals(listOf(3L), closed)
    }

    @Test
    fun zeroIsNotAHandle() {
        try {
            Camera(0)
            fail("no exception")
        } catch (e: IllegalArgumentException) {
            assertTrue(e.message!!.contains("no handle"))
        }
    }

    @Test
    fun manyThreadsClosingReleaseExactlyOnce() {
        repeat(200) {
            closed.clear()
            val camera = Camera(9)
            val start = CountDownLatch(1)
            val threads = List(4) { thread { start.await(); camera.close() } }
            start.countDown()
            threads.forEach { it.join() }
            assertEquals(listOf(9L), closed)
        }
    }

    @Test
    fun theGuardWatchesEveryPeerAndKeepsOnlyItsState() {
        val camera = Camera(5)
        assertEquals(1, guard.watched.size)
        assertSame(camera, guard.watched[0].first)
        assertEquals(5L, guard.watched[0].second.handle)
    }

    @Test
    fun aForgottenPeerIsClosedByTheGuardAndAClosedOneIsNot() {
        val warnings = mutableListOf<String>()
        LeakGuards.warnOnLeak = true
        val before = LeakGuards.warning
        LeakGuards.warning = { warnings += it }
        try {
            val forgotten = Camera(1)
            val closedByHand = Camera(2)
            closedByHand.close()

            LeakGuards.closeForgotten("Camera", guard.watched[0].second) // what the guard does after a GC
            LeakGuards.closeForgotten("Camera", guard.watched[1].second)

            assertEquals(listOf(2L, 1L), closed)
            assertEquals(1, warnings.size)
            assertTrue(warnings[0].contains("Camera was not closed"))
            assertTrue(forgotten.isClosed)
        } finally {
            LeakGuards.warning = before
        }
    }

    @Test
    fun aGuardDoesNotWarnWhenItIsOff() {
        val warnings = AtomicInteger()
        val before = LeakGuards.warning
        LeakGuards.warning = { warnings.incrementAndGet() }
        try {
            Camera(1)
            LeakGuards.closeForgotten("Camera", guard.watched[0].second)
            assertEquals(0, warnings.get())
            assertEquals(listOf(1L), closed)
        } finally {
            LeakGuards.warning = before
        }
    }
}
