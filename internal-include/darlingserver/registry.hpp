/**
 * This file is part of Darling.
 *
 * Copyright (C) 2021 Darling developers
 *
 * Darling is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Darling is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Darling.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _DARLINGSERVER_REGISTRY_HPP_
#define _DARLINGSERVER_REGISTRY_HPP_

#include <vector>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <shared_mutex>
#include <optional>
#include <functional>

#include <sys/types.h>

#include <darlingserver/message.hpp>
#include <darlingserver/process.hpp>
#include <darlingserver/thread.hpp>

namespace DarlingServer {
	template<class Entry>
	class Registry {
	private:
		std::unordered_map<typename Entry::ID, std::shared_ptr<Entry>> _map;
		std::unordered_map<typename Entry::NSID, std::shared_ptr<Entry>> _nsmap;
		mutable std::shared_mutex _rwlock;

		// sometimes, the factory used with registerIfAbsent needs to be able to look up other entries
		// (and the factory is called with the lock held, so trying to acquire the lock again would be a deadlock)
		static thread_local bool _registeringWithLockHeld;

	public:
		using ID = typename Entry::ID;
		using NSID = typename Entry::NSID;

		std::shared_ptr<Entry> registerIfAbsent(NSID nsid, std::function<std::shared_ptr<Entry>()> entryFactory) {
			std::unique_lock lock(_rwlock);

			auto it2 = _nsmap.find(nsid);
			if (it2 != _nsmap.end()) {
				return (*it2).second;
			}

			_registeringWithLockHeld = true;
			auto entry = entryFactory();
			_registeringWithLockHeld = false;
			_map[entry->id()] = entry;
			_nsmap[entry->nsid()] = entry;
			return entry;
		};

		bool registerEntry(std::shared_ptr<Entry> entry, bool replace = false) {
			std::unique_lock lock(_rwlock);

			if (!replace && (_map.find(entry->id()) != _map.end() || _nsmap.find(entry->nsid()) != _nsmap.end())) {
				return false;
			}

			_map[entry->id()] = entry;
			_nsmap[entry->nsid()] = entry;
			return true;
		};

		bool unregisterEntryByID(ID id) {
			std::unique_lock lock(_rwlock);

			auto it = _map.find(id);

			if (it == _map.end()) {
				return false;
			}

			std::shared_ptr<Entry> entry = (*it).second;
			auto it2 = _nsmap.find(entry->nsid());

			if (it2 == _nsmap.end()) {
				return false;
			}

			_map.erase(it);
			_nsmap.erase(it2);
			return true;
		};

		bool unregisterEntryByNSID(NSID nsid) {
			std::unique_lock lock(_rwlock);

			auto it2 = _nsmap.find(nsid);

			if (it2 == _nsmap.end()) {
				return false;
			}

			std::shared_ptr<Entry> entry = (*it2).second;
			auto it = _map.find(entry->id());

			if (it == _map.end()) {
				return false;
			}

			_map.erase(it);
			_nsmap.erase(it2);
			return true;
		};

		/**
		 * Unregisters the given entry from this Registry.
		 *
		 * This is the recommended method for unregistering entries as it will
		 * actually compare pointers to ensure the entry being unregistered is
		 * the same as the one currently registered with the same IDs.
		 */
		bool unregisterEntry(std::shared_ptr<Entry> entry) {
			std::unique_lock lock(_rwlock);

			auto it = _map.find(entry->id());
			auto it2 = _nsmap.find(entry->nsid());

			if (it == _map.end() || it2 == _nsmap.end()) {
				return false;
			}

			// note that we *want* pointer-to-pointer comparison
			if ((*it).second != entry || (*it2).second != entry) {
				return false;
			}

			_map.erase(it);
			_nsmap.erase(it2);
			return true;
		};

		std::optional<std::shared_ptr<Entry>> lookupEntryByID(ID id) {
			std::shared_lock lock(_rwlock, std::defer_lock);

			if (!_registeringWithLockHeld) {
				lock.lock();
			}

			auto it = _map.find(id);
			if (it == _map.end()) {
				return std::nullopt;
			}

			return (*it).second;
		};

		std::optional<std::shared_ptr<Entry>> lookupEntryByNSID(ID nsid) {
			std::shared_lock lock(_rwlock, std::defer_lock);

			if (!_registeringWithLockHeld) {
				lock.lock();
			}

			auto it2 = _nsmap.find(nsid);
			if (it2 == _nsmap.end()) {
				return std::nullopt;
			}

			return (*it2).second;
		};

		/**
		 * Locks the registry, preventing new entries from being added and old ones from being removed.
		 *
		 * This should be used very sparingly; you almost certainly don't want to use this.
		 * The primary use case for this is preventing an entry from being removed while it is being used
		 * in a situation where taking its shared_ptr is not possible.
		 *
		 * Every call to this method MUST be balanced with a call to unlock().
		 */
		void lock() const {
			_rwlock.lock();
		};

		void unlock() const {
			_rwlock.unlock();
		};

		using ScopedLock = std::unique_lock<std::shared_mutex>;

		/**
		 * Locks the registry like lock(), but employs the RAII idiom to automatically unlock it at the end of the scope.
		 *
		 * Scoped locks can also be manually unlocked earlier.
		 */
		ScopedLock scopedLock() const {
			return ScopedLock(_rwlock);
		};
	};

	Registry<Process>& processRegistry();
	Registry<Thread>& threadRegistry();
};

template<class T>
thread_local bool DarlingServer::Registry<T>::_registeringWithLockHeld = false;

#endif // _DARLINGSERVER_REGISTRY_HPP_
