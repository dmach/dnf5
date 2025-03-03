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

#ifndef LIBDNF_MODULE_MODULE_SACK_IMPL_HPP
#define LIBDNF_MODULE_MODULE_SACK_IMPL_HPP

#include "module/module_metadata.hpp"
#include "solv/solv_map.hpp"

#include "libdnf/module/module_sack.hpp"
#include "libdnf/rpm/reldep_list.hpp"

extern "C" {
#include <solv/pool.h>
}

#include <optional>

namespace libdnf::module {


class ModuleGoalPrivate;


class ModuleSack::Impl {
public:
    /// Create libsolv pool and set POOL_FLAG_WHATPROVIDESWITHDISABLED to ensure excluded packages are not taken as
    /// candidates for solver
    Impl(ModuleSack & module_sack, const BaseWeakPtr & base)
        : module_sack(&module_sack),
          base(base),
          module_metadata(base),
          pool(pool_create()) {
        pool_set_flag(pool, POOL_FLAG_WHATPROVIDESWITHDISABLED, 1);
    }
    ~Impl() { pool_free(pool); }

    const std::vector<std::unique_ptr<ModuleItem>> & get_modules();

    /// Apply modular filtering to package set. For proper functionality, all repositories must be loaded and active
    /// modules must be resolved (modular solver).
    void module_filtering();

    /// Supporting method that iterates over all modules and creates filtering sets for modular filtering
    /// include_NEVRAs contains artifacts from active modules
    /// exclude_NEVRAs contains artifacts from not active modules
    /// names and reldep_name_list contain names from include_NEVRAs that are not source
    /// src_names contains names from include_NEVRAs that are source
    ///
    /// @return <include_NEVRAs>, <exclude_NEVRAs>, <names>, <src_names>, <reldep_name_list>
    std::tuple<
        std::vector<std::string>,
        std::vector<std::string>,
        std::vector<std::string>,
        std::vector<std::string>,
        rpm::ReldepList>
    collect_data_for_modular_filtering();

    // Compute static context for older modules and move these modules to `ModuleSack.modules`.
    void add_modules_without_static_context();

    void make_provides_ready();
    void recompute_considered_in_pool();

    // Requires resolved goal. Takes list_installs() from goal and adds all modules with the same SOLVABLE_NAME
    // (<name>:<stream>:<context>) into active_modules.
    void set_active_modules(ModuleGoalPrivate & goal);

    /// Resolve given module items.
    ///
    /// @param module_items Module Items to resolve.
    /// @return `std::pair` of problems in resolving and ModuleErrorType.
    /// @since 5.0
    std::pair<std::vector<std::vector<std::string>>, ModuleSack::ModuleErrorType> module_solve(
        std::vector<ModuleItem *> module_items);

private:
    friend ModuleSack;
    friend ModuleItem;
    friend ModuleGoalPrivate;
    friend class ModuleQuery;

    ModuleSack * module_sack;
    BaseWeakPtr base;
    ModuleMetadata module_metadata;
    std::vector<std::unique_ptr<ModuleItem>> modules;
    Pool * pool;
    // Repositories containing any modules. Key is repoid, value is Id of the repo in libsolv.
    // This is needed in `ModuleItem::create_solvable` for creating solvable in the correct repository.
    std::map<std::string, Id> repositories;

    // Older ModuleItems that don't have static context. After all metadata are loaded, static contexts are assigned
    // also to these ModuleItems and they are removed from this vector and added to `ModuleSack.modules`.
    // This is done in `ModuleSack::add_modules_without_static_context`.
    std::vector<std::unique_ptr<ModuleItem>> modules_without_static_context;

    bool provides_ready = false;
    bool considered_uptodate = false;
    bool platform_detected = false;

    std::map<std::string, std::string> module_defaults;
    std::unique_ptr<libdnf::solv::SolvMap> excludes;
    std::map<Id, ModuleItem *> active_modules;

    /// @brief Method for autodetection of the platform id.
    /// @return If platform id was detected, it returns a pair where the first item is the platform
    ///         module name and second is the platform stream. Otherwise std::nullopt is returned.
    std::optional<std::pair<std::string, std::string>> detect_platform_name_and_stream() const;
};

inline const std::vector<std::unique_ptr<ModuleItem>> & ModuleSack::Impl::get_modules() {
    add_modules_without_static_context();
    return modules;
}

}  // namespace libdnf::module

#endif  // LIBDNF_MODULE_MODULE_SACK_IMPL_HPP
