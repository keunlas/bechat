#ifndef BECHAT_SERVICE_USER_REGISTRY_H_
#define BECHAT_SERVICE_USER_REGISTRY_H_

#include <sodium.h>

#include <mutex>
#include <string>
#include <unordered_map>

struct UserRecord {
  std::string username;
  std::string password_hash;
};

class UserRegisty {
 public:
  /**
   * @brief 注册用户
   *
   * @param username 用户名
   * @param password 密码
   * @return 状态码
   */
  int Signup(const std::string& username, const std::string& password);

  /**
   * @brief 验证用户
   *
   * @param username 用户名
   * @param password 密码
   * @return 状态码
   */
  int Verify(const std::string& username, const std::string& password);

 private:
  std::unordered_map<std::string /* username */, UserRecord> user_records_{};
  std::mutex user_records_mtx_{};
};

#endif  // !BECHAT_SERVICE_USER_REGISTRY_H_
