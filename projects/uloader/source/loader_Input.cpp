#include <ul/loader/loader_Input.hpp>
#include <ul/loader/loader_ProgramIdUtils.hpp>
#include <ul/loader/loader_Common.hpp>

#include <cstring>
#include <memory>

namespace ul::loader {

    Result ReadTargetInput(TargetInput &out_target_ipt) {
        UL_RC_TRY(appletInitialize());
        UL_ON_SCOPE_EXIT(appletExit());

        AppletStorage target_ipt_storage;
        if (SelfIsApplet()) {
            // The common args storage is not used for the applet variant.
            AppletStorage common_args_storage;
            UL_RC_TRY(appletPopInData(&common_args_storage));
            appletStorageClose(&common_args_storage);

            UL_RC_TRY(appletPopInData(&target_ipt_storage));
        } else if (SelfIsApplication()) {
            UL_RC_TRY(appletPopLaunchParameter(&target_ipt_storage, AppletLaunchParameterKind_UserChannel));
        } else {
            return ResultInvalidProcessType;
        }
        UL_ON_SCOPE_EXIT(appletStorageClose(&target_ipt_storage));

        s64 storage_size;
        UL_RC_TRY(appletStorageGetSize(&target_ipt_storage, &storage_size));
        if (static_cast<size_t>(storage_size) >= sizeof(out_target_ipt)) {
            UL_RC_TRY(appletStorageRead(&target_ipt_storage, 0, std::addressof(out_target_ipt), sizeof(out_target_ipt)));
            if (out_target_ipt.magic == TargetInput::Magic) {
                // By default, argv = <nro-path>.
                if (std::strlen(out_target_ipt.nro_argv) == 0) {
                    CopyToStringBuffer(out_target_ipt.nro_argv, out_target_ipt.nro_path);
                }

                return ResultSuccess;
            } else {
                return ResultInvalidTargetInputMagic;
            }
        } else {
            return ResultInvalidTargetInputSize;
        }
    }

    Result WriteTargetOutput(const TargetOutput &target_opt) {
        UL_RC_TRY(appletInitialize());
        UL_ON_SCOPE_EXIT(appletExit());

        if (SelfIsApplet()) {
            AppletStorage target_opt_st;
            UL_RC_TRY(appletCreateStorage(&target_opt_st, sizeof(target_opt)));
            UL_RC_TRY(appletStorageWrite(&target_opt_st, 0, std::addressof(target_opt), sizeof(target_opt)));

            UL_RC_TRY(appletPushOutData(&target_opt_st));
        } else {
            return ResultInvalidProcessType;
        }

        return ResultSuccess;
    }

}
