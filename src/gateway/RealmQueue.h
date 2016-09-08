/*
 * Copyright (c) 2016 Ember
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <shared/ClientUUID.h>
#include <boost/asio/basic_waitable_timer.hpp>
#include <boost/asio/io_service.hpp>
#include <chrono>
#include <list>
#include <functional>
#include <memory>
#include <mutex>
#include <cstddef>

namespace ember {

class ClientConnection;

class RealmQueue {
	typedef std::function<void()> LeaveQueueCB;

	struct QueueEntry {
		int priority;
		ClientUUID client;
		LeaveQueueCB callback;

		bool operator>(const QueueEntry& rhs) const {
			return rhs.priority > priority;
		}

		bool operator<(const QueueEntry& rhs) const {
			return rhs.priority < priority;
		}
	};

	const std::chrono::milliseconds TIMER_FREQUENCY { 250 };

	boost::asio::basic_waitable_timer<std::chrono::steady_clock> timer_;
	std::list<QueueEntry> queue_;
	std::mutex lock_;

	void send_position(std::size_t position, ClientUUID client);
	void update_clients();
	void set_timer();

public:
	RealmQueue::RealmQueue(boost::asio::io_service& service) : timer_(service) { }

	void enqueue(ClientUUID client, LeaveQueueCB callback, int priority = 0);
	void dequeue(const ClientUUID& client);
	void free_slot();
	void shutdown();
	std::size_t size() const;
};

} // ember