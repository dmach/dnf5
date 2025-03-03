/*
Copyright Contributors to the libdnf project.

This file is part of libdnf: https://github.com/rpm-software-management/libdnf/

Libdnf is free software: you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation, either version 2.1 of the License, or
(at your option) any later version.

Libdnf is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License
along with libdnf.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "libdnf/repo/package_downloader.hpp"

#include "repo_downloader.hpp"
#include "utils/bgettext/bgettext-mark-domain.h"

#include "libdnf/base/base.hpp"
#include "libdnf/common/exception.hpp"
#include "libdnf/repo/download_callbacks.hpp"
#include "libdnf/repo/repo.hpp"

#include <librepo/librepo.h>

#include <filesystem>


namespace std {

template <>
struct default_delete<LrPackageTarget> {
    void operator()(LrPackageTarget * ptr) noexcept { lr_packagetarget_free(ptr); }
};

}  // namespace std


namespace libdnf::repo {

class PackageTarget {
public:
    PackageTarget(const libdnf::rpm::Package & package, const std::string & destination, void * user_data)
        : package(package),
          destination(destination),
          user_data(user_data) {}

    libdnf::rpm::Package package;
    std::string destination;
    void * user_data;
    void * user_cb_data{nullptr};
};

static int end_callback(void * data, LrTransferStatus status, const char * msg) {
    libdnf_assert(data != nullptr, "data in callback must be set");

    auto * package_target = static_cast<PackageTarget *>(data);
    auto cb_status = static_cast<DownloadCallbacks::TransferStatus>(status);
    if (auto * download_callbacks = package_target->package.get_base()->get_download_callbacks()) {
        return download_callbacks->end(package_target->user_cb_data, cb_status, msg);
    }
    return 0;
}

static int progress_callback(void * data, double total_to_download, double downloaded) {
    libdnf_assert(data != nullptr, "data in callback must be set");

    auto * package_target = static_cast<PackageTarget *>(data);
    if (auto * download_callbacks = package_target->package.get_base()->get_download_callbacks()) {
        return download_callbacks->progress(package_target->user_cb_data, total_to_download, downloaded);
    }
    return 0;
}

static int mirror_failure_callback(void * data, const char * msg, const char * url) {
    libdnf_assert(data != nullptr, "data in callback must be set");

    auto * package_target = static_cast<PackageTarget *>(data);
    if (auto * download_callbacks = package_target->package.get_base()->get_download_callbacks()) {
        return download_callbacks->mirror_failure(package_target->user_cb_data, msg, url);
    }
    return 0;
}


class PackageDownloader::Impl {
    friend PackageDownloader;
    std::vector<PackageTarget> targets;
};


PackageDownloader::PackageDownloader() : p_impl(std::make_unique<Impl>()) {}
PackageDownloader::~PackageDownloader() = default;


void PackageDownloader::add(const libdnf::rpm::Package & package, void * user_data) {
    add(package, std::filesystem::path(package.get_repo()->get_cachedir()) / "packages", user_data);
}


void PackageDownloader::add(const libdnf::rpm::Package & package, const std::string & destination, void * user_data) {
    p_impl->targets.emplace_back(package, destination, user_data);
}


void PackageDownloader::download(bool fail_fast, bool resume) try {
    GError * err{nullptr};

    std::vector<std::unique_ptr<LrPackageTarget>> lr_targets;
    lr_targets.reserve(p_impl->targets.size());
    for (auto & pkg_target : p_impl->targets) {
        std::filesystem::create_directory(pkg_target.destination);

        if (auto * download_callbacks = pkg_target.package.get_base()->get_download_callbacks()) {
            pkg_target.user_cb_data = download_callbacks->add_new_download(
                pkg_target.user_data,
                pkg_target.package.get_full_nevra().c_str(),
                static_cast<double>(pkg_target.package.get_package_size()));
        }

        auto * lr_target = lr_packagetarget_new_v3(
            pkg_target.package.get_repo()->downloader->get_cached_handle().get(),
            pkg_target.package.get_location().c_str(),
            pkg_target.destination.c_str(),
            static_cast<LrChecksumType>(pkg_target.package.get_checksum().get_type()),
            pkg_target.package.get_checksum().get_checksum().c_str(),
            static_cast<int64_t>(pkg_target.package.get_package_size()),
            pkg_target.package.get_baseurl().empty() ? nullptr : pkg_target.package.get_baseurl().c_str(),
            resume,
            progress_callback,
            &pkg_target,
            end_callback,
            mirror_failure_callback,
            0,
            0,
            &err);

        if (!lr_target) {
            throw LibrepoError(std::unique_ptr<GError>(err));
        }

        lr_targets.emplace_back(lr_target);
    }

    // Adding items to the end of GSList is slow. We go from the back and add items to the beginning.
    GSList * list{nullptr};
    for (auto it = lr_targets.rbegin(); it != lr_targets.rend(); ++it) {
        list = g_slist_prepend(list, it->get());
    }
    std::unique_ptr<GSList, decltype(&g_slist_free)> list_holder(list, &g_slist_free);

    LrPackageDownloadFlag flags = static_cast<LrPackageDownloadFlag>(0);
    if (fail_fast) {
        flags = static_cast<LrPackageDownloadFlag>(flags | LR_PACKAGEDOWNLOAD_FAILFAST);
    }

    if (!lr_download_packages(list, flags, &err)) {
        throw LibrepoError(std::unique_ptr<GError>(err));
    }
} catch (const std::runtime_error & e) {
    throw_with_nested(PackageDownloadError(M_("Failed to download packages")));
}

}  // namespace libdnf::repo
