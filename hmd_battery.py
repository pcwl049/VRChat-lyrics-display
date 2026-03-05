"""
VR头显电量监测模块
通过 OpenVR/SteamVR API 获取头显电量信息
支持 Meta Quest、Valve Index、HTC Vive 等头显

当 OpenVR 被占用时，备选方案：通过 ADB 获取 Quest 电量
"""
import time
import subprocess
from dataclasses import dataclass
from typing import Optional
from enum import Enum


class BatteryStatus(Enum):
    """电池状态枚举"""
    CHARGING = "charging"
    DISCHARGING = "discharging"
    FULL = "full"
    UNKNOWN = "unknown"


@dataclass
class HMDBatteryInfo:
    """头显电量信息"""
    level: float  # 电量百分比 0.0-1.0
    is_charging: bool
    status: BatteryStatus
    
    @property
    def level_percent(self) -> int:
        """电量百分比整数"""
        return int(self.level * 100)
    
    @property
    def icon(self) -> str:
        """电量图标"""
        if self.is_charging:
            return "🔌"
        
        level = self.level_percent
        if level >= 80:
            return "🔋"
        elif level >= 60:
            return "🔋"
        elif level >= 40:
            return "🪫"
        elif level >= 20:
            return "🪫"
        else:
            return "⚠️"
    
    @property
    def color_indicator(self) -> str:
        """颜色指示（用于终端显示）"""
        level = self.level_percent
        if self.is_charging:
            return "🔌 充电中"
        elif level >= 60:
            return f"🔋 {level}%"
        elif level >= 30:
            return f"🪫 {level}%"
        else:
            return f"⚠️ {level}%"
    
    def __str__(self) -> str:
        if self.is_charging:
            return f"🔌 {self.level_percent}%"
        return f"🔋 {self.level_percent}%"


class ADBBatteryMonitor:
    """通过 ADB 获取 Quest 电量（备选方案）"""
    
    def __init__(self):
        self._adb_path = "adb"
        self._available = None
        self._last_check_time = 0
        self._cached_info: Optional[HMDBatteryInfo] = None
        self._check_interval = 10.0
    
    def _run_adb(self, args: list, timeout: float = 3.0) -> tuple[bool, str]:
        """运行 ADB 命令"""
        try:
            result = subprocess.run(
                [self._adb_path] + args,
                capture_output=True,
                text=True,
                timeout=timeout
            )
            return result.returncode == 0, result.stdout.strip()
        except FileNotFoundError:
            return False, "ADB 未安装"
        except subprocess.TimeoutExpired:
            return False, "ADB 超时"
        except Exception as e:
            return False, str(e)
    
    def check_available(self) -> bool:
        """检查 ADB 是否可用且有设备连接"""
        if self._available is not None:
            return self._available
        
        success, output = self._run_adb(["devices"])
        if not success:
            self._available = False
            return False
        
        # 检查是否有设备连接（排除标题行）
        lines = output.split('\n')
        device_count = 0
        for line in lines[1:]:
            if '\t' in line and 'device' in line:
                device_count += 1
        
        self._available = device_count > 0
        return self._available
    
    def get_battery_info(self, force_refresh: bool = False) -> Optional[HMDBatteryInfo]:
        """获取 Quest 电量"""
        current_time = time.time()
        
        if not force_refresh and self._cached_info and (current_time - self._last_check_time < self._check_interval):
            return self._cached_info
        
        if not self.check_available():
            return None
        
        # 获取电量百分比
        success, level_output = self._run_adb([
            "shell", "dumpsys", "battery", "|", "grep", "level"
        ])
        
        # Windows 上管道符不工作，用另一种方式
        success, battery_output = self._run_adb([
            "shell", "dumpsys", "battery"
        ])
        
        if not success:
            return self._cached_info
        
        level = None
        charging = False
        
        for line in battery_output.split('\n'):
            line = line.strip()
            if line.startswith('level:'):
                try:
                    level = int(line.split(':')[1].strip()) / 100.0
                except (ValueError, IndexError):
                    pass
            elif line.startswith('status:'):
                try:
                    status_code = int(line.split(':')[1].strip())
                    # 2 = charging, 5 = full
                    charging = status_code in [2, 5]
                except (ValueError, IndexError):
                    pass
        
        if level is not None:
            status = BatteryStatus.CHARGING if charging else (
                BatteryStatus.FULL if level >= 0.99 else BatteryStatus.DISCHARGING
            )
            
            self._cached_info = HMDBatteryInfo(
                level=level,
                is_charging=charging,
                status=status
            )
            self._last_check_time = current_time
            return self._cached_info
        
        return self._cached_info


class HMDBatteryMonitor:
    """头显电量监测器（支持 OpenVR 和 ADB 两种方式）"""
    
    def __init__(self):
        self._vr_system = None
        self._initialized = False
        self._last_check_time = 0
        self._cached_info: Optional[HMDBatteryInfo] = None
        self._check_interval = 5.0  # 电量检查间隔（秒）
        self._available = False
        self._init_error: Optional[str] = None
        self._use_adb = False  # 是否使用 ADB 模式
        self._adb_monitor: Optional[ADBBatteryMonitor] = None
    
    def init(self) -> bool:
        """
        初始化电量监测
        
        优先尝试 OpenVR，失败后尝试 ADB
        
        Returns:
            bool: 是否初始化成功
        """
        if self._initialized:
            return self._available
        
        # 先尝试 OpenVR
        if self._init_openvr():
            self._initialized = True
            self._available = True
            self._use_adb = False
            return True
        
        # OpenVR 失败，尝试 ADB
        if self._init_adb():
            self._initialized = True
            self._available = True
            self._use_adb = True
            self._init_error = None  # 清除错误，因为 ADB 可用
            return True
        
        # 两种方式都失败
        self._initialized = True
        self._available = False
        return False
    
    def _init_openvr(self) -> bool:
        """尝试初始化 OpenVR"""
        try:
            import openvr
            
            # 尝试不同的应用类型初始化
            init_types = [
                (openvr.VRApplication_Overlay, "Overlay"),
                (openvr.VRApplication_Background, "Background"),
                (openvr.VRApplication_Other, "Other"),
            ]
            
            for app_type, type_name in init_types:
                try:
                    import threading
                    result = {'vr_system': None, 'error': None}
                    
                    def try_init():
                        try:
                            result['vr_system'] = openvr.init(app_type)
                        except Exception as e:
                            result['error'] = e
                    
                    thread = threading.Thread(target=try_init, daemon=True)
                    thread.start()
                    thread.join(timeout=2.0)
                    
                    if thread.is_alive():
                        continue
                    
                    if result['error']:
                        raise result['error']
                    
                    if result['vr_system']:
                        self._vr_system = result['vr_system']
                        return True
                        
                except openvr.error_code.InitError_IPC_NamespaceUnavailable:
                    continue
                except openvr.error_code.InitError_Init_NoServerForBackgroundApp:
                    continue
                except Exception:
                    continue
            
            self._init_error = "OpenVR 被 VRChat 占用"
            return False
            
        except ImportError:
            self._init_error = "未安装 openvr"
            return False
        except Exception as e:
            self._init_error = f"OpenVR 初始化失败: {e}"
            return False
    
    def _init_adb(self) -> bool:
        """尝试初始化 ADB"""
        self._adb_monitor = ADBBatteryMonitor()
        if self._adb_monitor.check_available():
            return True
        self._adb_monitor = None
        return False
    
    def shutdown(self):
        """关闭监测器"""
        if self._vr_system:
            try:
                import openvr
                openvr.shutdown()
            except Exception:
                pass
        self._vr_system = None
        self._adb_monitor = None
        self._initialized = False
        self._available = False
    
    @property
    def is_available(self) -> bool:
        """电量监测是否可用"""
        return self._available
    
    @property
    def init_error(self) -> Optional[str]:
        """初始化错误信息"""
        return self._init_error
    
    @property
    def mode(self) -> str:
        """当前使用的模式"""
        if not self._available:
            return "不可用"
        return "ADB" if self._use_adb else "OpenVR"
    
    def get_battery_info(self, force_refresh: bool = False) -> Optional[HMDBatteryInfo]:
        """
        获取头显电量信息
        
        Args:
            force_refresh: 是否强制刷新（忽略缓存）
        
        Returns:
            Optional[HMDBatteryInfo]: 电量信息，失败返回 None
        """
        # 检查缓存
        current_time = time.time()
        if not force_refresh and self._cached_info and (current_time - self._last_check_time < self._check_interval):
            return self._cached_info
        
        # 确保已初始化
        if not self._initialized:
            if not self.init():
                return None
        
        if not self._available:
            return None
        
        # 根据模式选择获取方式
        if self._use_adb and self._adb_monitor:
            info = self._adb_monitor.get_battery_info(force_refresh)
            if info:
                self._cached_info = info
                self._last_check_time = current_time
            return info
        
        # OpenVR 模式
        if not self._vr_system:
            return None
        
        try:
            import openvr
            
            hmd_index = openvr.k_unTrackedDeviceIndex_Hmd
            
            if not self._vr_system.isTrackedDeviceConnected(hmd_index):
                return None
            
            level = self._vr_system.getFloatTrackedDeviceProperty(
                hmd_index,
                openvr.Prop_DeviceBatteryPercentage_Float
            )
            
            is_charging = self._vr_system.getBoolTrackedDeviceProperty(
                hmd_index,
                openvr.Prop_DeviceIsCharging_Bool
            )
            
            if is_charging:
                status = BatteryStatus.CHARGING
            elif level >= 0.99:
                status = BatteryStatus.FULL
            else:
                status = BatteryStatus.DISCHARGING
            
            self._cached_info = HMDBatteryInfo(
                level=level,
                is_charging=is_charging,
                status=status
            )
            self._last_check_time = current_time
            
            return self._cached_info
            
        except Exception:
            return self._cached_info
    
    def get_controller_battery(self, controller_index: int = 0) -> Optional[HMDBatteryInfo]:
        """
        获取控制器电量信息
        
        Args:
            controller_index: 控制器索引 (0=左手, 1=右手)
        
        Returns:
            Optional[HMDBatteryInfo]: 电量信息
        """
        if not self._initialized:
            if not self.init():
                return None
        
        if not self._available or not self._vr_system:
            return None
        
        try:
            import openvr
            
            # 查找控制器设备
            controller_count = 0
            for i in range(openvr.k_unMaxTrackedDeviceCount):
                device_class = self._vr_system.getTrackedDeviceClass(i)
                if device_class == openvr.TrackedDeviceClass_Controller:
                    if controller_count == controller_index:
                        # 找到目标控制器
                        level = self._vr_system.getFloatTrackedDeviceProperty(
                            i,
                            openvr.Prop_DeviceBatteryPercentage_Float
                        )
                        is_charging = self._vr_system.getBoolTrackedDeviceProperty(
                            i,
                            openvr.Prop_DeviceIsCharging_Bool
                        )
                        
                        status = BatteryStatus.CHARGING if is_charging else (
                            BatteryStatus.FULL if level >= 0.99 else BatteryStatus.DISCHARGING
                        )
                        
                        return HMDBatteryInfo(
                            level=level,
                            is_charging=is_charging,
                            status=status
                        )
                    controller_count += 1
            
            return None
            
        except Exception:
            return None


# 全局实例
_monitor: Optional[HMDBatteryMonitor] = None


def get_monitor() -> HMDBatteryMonitor:
    """获取全局电量监测器实例"""
    global _monitor
    if _monitor is None:
        _monitor = HMDBatteryMonitor()
    return _monitor


def get_hmd_battery(force_refresh: bool = False) -> Optional[HMDBatteryInfo]:
    """
    快捷函数：获取头显电量
    
    Args:
        force_refresh: 是否强制刷新
    
    Returns:
        Optional[HMDBatteryInfo]: 电量信息
    """
    monitor = get_monitor()
    return monitor.get_battery_info(force_refresh)


def get_battery_display(force_refresh: bool = False) -> str:
    """
    获取电量显示字符串（用于聊天框）
    
    Args:
        force_refresh: 是否强制刷新
    
    Returns:
        str: 电量显示字符串，如 "🔋85%"
    """
    info = get_hmd_battery(force_refresh)
    if info:
        return str(info)
    return ""


if __name__ == '__main__':
    from ui import print_status, print_banner, colorize, Color
    
    print_banner()
    print("VR头显电量监测测试")
    print("-" * 40)
    
    monitor = HMDBatteryMonitor()
    
    if monitor.init():
        print_status('success', f"电量监测已启用 (模式: {monitor.mode})")
        
        info = monitor.get_battery_info(force_refresh=True)
        if info:
            print()
            print(f"头显电量: {info.icon} {info.level_percent}%")
            print(f"充电状态: {'充电中' if info.is_charging else '使用电池'}")
            
            # 持续监测
            print()
            print("持续监测中... (按 Ctrl+C 停止)")
            try:
                while True:
                    info = monitor.get_battery_info()
                    if info:
                        print(f"\r{info.color_indicator}   ", end="", flush=True)
                    time.sleep(1)
            except KeyboardInterrupt:
                print()
        else:
            print_status('warning', "无法获取头显电量，请确保头显已连接")
        
        monitor.shutdown()
    else:
        print_status('error', monitor.init_error)
        print()
        print("解决方案:")
        print("1. OpenVR 模式: 确保 SteamVR 正在运行且头显已连接")
        print("2. ADB 模式: 安装 ADB 并连接 Quest 设备")
        print("   - 开启 Quest 开发者模式")
        print("   - USB 连接: adb devices")
        print("   - 无线连接: adb connect <Quest IP>:5555")
