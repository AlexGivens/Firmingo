#pragma once
#include "core/upload.h"
#include "switch.h"

namespace firmingo_managed {
// One boot-lifetime transaction record and a 4 KiB RAM stage. The only flash
// work occurs in poll(), which the resident calls outside the Ethernet lock.
class FmgoUpload final : public firmingo::UploadService {
 public:
  using Check = bool (*)();
  FmgoUpload(CapsuleStage& stage, SketchSwitch& switching, uint32_t api_address,
             Check armed, Check generation_returned)
      : stage_(stage), switching_(switching), api_(api_address),
        armed_(armed), generation_returned_(generation_returned) {}
  const firmingo::UploadTarget& target() const override;
  firmingo::UploadError begin(uint32_t owner, uint32_t size, const uint8_t digest[32], firmingo::UploadStatus&) override;
  firmingo::UploadError chunk(uint32_t owner, uint32_t id, uint32_t offset,
                              const uint8_t* data, std::size_t size, firmingo::UploadStatus&) override;
  firmingo::UploadError finish(uint32_t owner, uint32_t id, firmingo::UploadStatus&) override;
  firmingo::UploadError commit(uint32_t owner, uint32_t id, firmingo::UploadStatus&) override;
  firmingo::UploadError abort(uint32_t owner, uint32_t id, firmingo::UploadStatus&) override;
  firmingo::UploadError status(uint32_t id, firmingo::UploadStatus&) const override;
  void disconnect(uint32_t owner) override;
  bool blocks_console() const override;
  void poll(uint32_t now);
 private:
  firmingo::UploadError check(uint32_t owner, uint32_t id, firmingo::UploadState expected) const;
  void fail(firmingo::UploadError error);
  CapsuleStage& stage_;
  SketchSwitch& switching_;
  const uint32_t api_;
  const Check armed_, generation_returned_;
  uint32_t owner_ = 0, next_id_ = 1;
  bool commit_pending_ = false;
  firmingo::UploadStatus last_{};
};
}  // namespace firmingo_managed
