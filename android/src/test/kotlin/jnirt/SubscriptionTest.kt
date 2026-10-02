package jnirt

import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import java.util.concurrent.Executor

/**
 * [Delivery] and [Subscription] without a native library: the native close and the leak guard are fakes (PeerRuntime), and
 * the executor is a queue the test runs by hand, like a Looper that the test controls.
 */
class SubscriptionTest {

    private class Owner(handle: Long) : NativePeer(handle)

    private class Queue : Executor {
        val tasks = ArrayDeque<Runnable>()
        override fun execute(command: Runnable) {
            tasks.addLast(command)
        }

        fun runAll() {
            while (tasks.isNotEmpty()) tasks.removeFirst().run()
        }
    }

    private val closed = mutableListOf<Long>()

    @Before
    fun fakeTheNativeSide() {
        PeerRuntime.release = { synchronized(closed) { closed += it } }
        PeerRuntime.guard = object : LeakGuard {
            override fun watch(owner: Any, state: PeerState) {}
        }
    }

    @After
    fun putTheRealOnesBack() {
        PeerRuntime.release = { NativePeerNatives.close(it) }
        PeerRuntime.guard = LeakGuards.default()
    }

    @Test
    fun postRunsOnTheExecutorNotOnTheCallingThread() {
        val queue = Queue()
        val seen = mutableListOf<Int>()
        val delivery = Delivery(queue)
        delivery.post { seen += 1 }
        assertTrue(seen.isEmpty())
        queue.runAll()
        assertEquals(listOf(1), seen)
    }

    @Test
    fun eventsKeepTheOrderOfTheExecutor() {
        val queue = Queue()
        val seen = mutableListOf<Int>()
        val delivery = Delivery(queue)
        (1..3).forEach { n -> delivery.post { seen += n } }
        queue.runAll()
        assertEquals(listOf(1, 2, 3), seen)
    }

    @Test
    fun shutdownDropsTheEventsThatWereWaiting() {
        val queue = Queue()
        val seen = mutableListOf<Int>()
        val delivery = Delivery(queue)
        delivery.post { seen += 1 }
        delivery.shutdown()
        delivery.post { seen += 2 }
        queue.runAll()
        assertTrue(seen.isEmpty())
    }

    @Test
    fun postLatestKeepsOnlyTheNewestWaitingEvent() {
        val queue = Queue()
        val seen = mutableListOf<Int>()
        val delivery = Delivery(queue)
        (1..5).forEach { n -> delivery.postLatest { seen += n } }
        assertEquals(1, queue.tasks.size)
        queue.runAll()
        assertEquals(listOf(5), seen)
        delivery.postLatest { seen += 6 }   // the slot is free again
        queue.runAll()
        assertEquals(listOf(5, 6), seen)
    }

    @Test
    fun postBufferedKeepsTheNewestEventsAndRunsOnePerTurn() {
        val queue = Queue()
        val seen = mutableListOf<Int>()
        val delivery = Delivery(queue)
        (1..5).forEach { n -> delivery.postBuffered(3) { seen += n } }
        assertEquals(1, queue.tasks.size)
        queue.tasks.removeFirst().run()                 // one turn: one event, and the next turn is scheduled
        assertEquals(listOf(3), seen)
        assertEquals(1, queue.tasks.size)
        queue.runAll()
        assertEquals(listOf(3, 4, 5), seen)
        delivery.postBuffered(3) { seen += 6 }          // the queue is idle again
        queue.runAll()
        assertEquals(listOf(3, 4, 5, 6), seen)
    }

    @Test
    fun postBufferedDoesNotStallAfterABlockThrew() {
        val queue = Queue()
        val seen = mutableListOf<Int>()
        val delivery = Delivery(queue)
        delivery.postBuffered(3) { error("listener bug") }
        delivery.postBuffered(3) { seen += 2 }
        try {
            queue.tasks.removeFirst().run()
            org.junit.Assert.fail("the exception must reach the executor")
        } catch (expected: IllegalStateException) {
        }
        queue.runAll()
        assertEquals(listOf(2), seen)
        delivery.postBuffered(3) { seen += 3 }
        queue.runAll()
        assertEquals(listOf(2, 3), seen)
    }

    @Test
    fun shutdownDropsTheBufferedEvents() {
        val queue = Queue()
        val seen = mutableListOf<Int>()
        val delivery = Delivery(queue)
        delivery.postBuffered(3) { seen += 1 }
        delivery.shutdown()
        delivery.postBuffered(3) { seen += 2 }
        queue.runAll()
        assertTrue(seen.isEmpty())
    }

    @Test
    fun closingASubscriptionDropsItsQueuedEventsAndClosesItsHandle() {
        val queue = Queue()
        val seen = mutableListOf<Int>()
        val delivery = Delivery(queue)
        val owner = Owner(1)
        val subscription = Subscription(owner, 2, delivery)
        delivery.post { seen += 1 }
        subscription.close()
        subscription.close()
        queue.runAll()
        assertTrue(seen.isEmpty())
        assertEquals(listOf(2L), closed)
        assertTrue(subscription.isClosed)
    }

    @Test
    fun closingTheOwnerClosesItsSubscriptionsAndQueuedEventsAreDropped() {
        val queue = Queue()
        val seen = mutableListOf<Int>()
        val delivery = Delivery(queue)
        val owner = Owner(1)
        val subscription = Subscription(owner, 2, delivery)
        delivery.post { seen += 1 }
        owner.close()
        queue.runAll()
        assertTrue(seen.isEmpty())
        assertTrue(subscription.isClosed)
        assertEquals(setOf(1L, 2L), closed.toSet())
        assertEquals(2, closed.size)
    }

    @Test
    fun aSubscriptionClosedByItselfIsNotClosedAgainByTheOwner() {
        val owner = Owner(1)
        Subscription(owner, 2, Delivery(Queue())).close()
        owner.close()
        assertEquals(listOf(2L, 1L), closed)
    }

    @Test
    fun aSubscriptionOfAnOwnerThatIsAlreadyClosedIsClosedAtOnce() {
        val owner = Owner(1)
        owner.close()
        val subscription = Subscription(owner, 2, Delivery(Queue()))
        assertTrue(subscription.isClosed)
        assertEquals(listOf(1L, 2L), closed)
    }

    @Test
    fun aSubscriptionCanCloseItselfFromInsideItsListener() {
        val queue = Queue()
        val delivery = Delivery(queue)
        lateinit var subscription: Subscription
        subscription = Subscription(Owner(1), 2, delivery)
        delivery.post { subscription.close() }
        delivery.post { error("not reached: the subscription is closed") }
        queue.runAll()
        assertTrue(subscription.isClosed)
    }
}
