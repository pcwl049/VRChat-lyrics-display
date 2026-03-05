"""
Voicemeeter 控制模块
通过 pyvoicemeeter 控制 Voicemeeter 的音频路由
支持一键开关音乐通道
"""
import time
from dataclasses import dataclass
from enum import Enum
from typing import Optional


class VoicemeeterType(Enum):
    """Voicemeeter版本类型"""
    STANDARD = "voicemeeter"
    BANANA = "voicemeeterbanana"
    POTATO = "voicemeeterpotato"


@dataclass
class VMStatus:
    """Voicemeeter状态"""
    is_connected: bool
    vm_type: Optional[str]
    message: str


class VoicemeeterController:
    """Voicemeeter控制器"""
    
    def __init__(self):
        self._vm = None
        self._vm_type = None
        self._connected = False
        
    def connect(self) -> bool:
        """
        连接到Voicemeeter
        
        Returns:
            bool: 是否连接成功
        """
        try:
            import voicemeeter_api as voicemeeter
            
            # 尝试连接不同版本的Voicemeeter
            for vm_type in ["potato", "banana", "basic"]:
                try:
                    self._vm = voicemeeter.remote(vm_type)
                    self._vm.login()
                    self._vm_type = vm_type
                    self._connected = True
                    return True
                except Exception:
                    continue
            
            return False
            
        except ImportError:
            print("请安装 voicemeeter-api: pip install voicemeeter-api")
            return False
        except Exception as e:
            print(f"连接Voicemeeter失败: {e}")
            return False
    
    def disconnect(self):
        """断开连接"""
        if self._vm and self._connected:
            try:
                self._vm.logout()
            except Exception:
                pass
        self._connected = False
        self._vm = None
    
    @property
    def is_connected(self) -> bool:
        """是否已连接"""
        return self._connected and self._vm is not None
    
    def get_status(self) -> VMStatus:
        """获取连接状态"""
        if self._connected:
            return VMStatus(
                is_connected=True,
                vm_type=self._vm_type,
                message=f"Voicemeeter {self._vm_type} 已连接"
            )
        else:
            return VMStatus(
                is_connected=False,
                vm_type=None,
                message="Voicemeeter未连接"
            )
    
    def toggle_mute(self, channel_type: str = "hardware", channel_index: int = 0) -> Optional[bool]:
        """
        切换通道静音状态
        
        Args:
            channel_type: 通道类型 "hardware"(硬件输入), "virtual"(虚拟输入), "output"(输出)
            channel_index: 通道索引 (从0开始)
        
        Returns:
            Optional[bool]: 切换后的静音状态，None表示失败
        """
        if not self.is_connected:
            return None
        
        try:
            self._vm.invalidate()
            
            if channel_type == "hardware":
                # 硬件输入通道 (如麦克风、线路输入)
                channel = self._vm.inputs[channel_index]
            elif channel_type == "virtual":
                # 虚拟输入通道 (如音乐软件输出)
                channel = self._vm.inputs[self._vm.phys_in + channel_index]
            elif channel_type == "output":
                # 输出通道
                channel = self._vm.outputs[channel_index]
            else:
                return None
            
            # 切换静音
            current_mute = channel.mute
            channel.mute = not current_mute
            
            return channel.mute
            
        except Exception as e:
            print(f"切换静音失败: {e}")
            return None
    
    def set_mute(self, mute: bool, channel_type: str = "hardware", channel_index: int = 0) -> bool:
        """
        设置通道静音状态
        
        Args:
            mute: 是否静音
            channel_type: 通道类型
            channel_index: 通道索引
        
        Returns:
            bool: 是否成功
        """
        if not self.is_connected:
            return False
        
        try:
            self._vm.invalidate()
            
            if channel_type == "hardware":
                channel = self._vm.inputs[channel_index]
            elif channel_type == "virtual":
                channel = self._vm.inputs[self._vm.phys_in + channel_index]
            elif channel_type == "output":
                channel = self._vm.outputs[channel_index]
            else:
                return False
            
            channel.mute = mute
            return True
            
        except Exception as e:
            print(f"设置静音失败: {e}")
            return False
    
    def get_mute(self, channel_type: str = "hardware", channel_index: int = 0) -> Optional[bool]:
        """
        获取通道静音状态
        
        Returns:
            Optional[bool]: 静音状态，None表示失败
        """
        if not self.is_connected:
            return None
        
        try:
            self._vm.invalidate()
            
            if channel_type == "hardware":
                channel = self._vm.inputs[channel_index]
            elif channel_type == "virtual":
                channel = self._vm.inputs[self._vm.phys_in + channel_index]
            elif channel_type == "output":
                channel = self._vm.outputs[channel_index]
            else:
                return None
            
            return channel.mute
            
        except Exception as e:
            print(f"获取静音状态失败: {e}")
            return None
    
    def set_volume(self, volume: float, channel_type: str = "hardware", channel_index: int = 0) -> bool:
        """
        设置通道音量
        
        Args:
            volume: 音量值 (0.0 - 1.0)
            channel_type: 通道类型
            channel_index: 通道索引
        
        Returns:
            bool: 是否成功
        """
        if not self.is_connected:
            return False
        
        try:
            self._vm.invalidate()
            volume = max(0.0, min(1.0, volume))
            
            if channel_type == "hardware":
                channel = self._vm.inputs[channel_index]
            elif channel_type == "virtual":
                channel = self._vm.inputs[self._vm.phys_in + channel_index]
            elif channel_type == "output":
                channel = self._vm.outputs[channel_index]
            else:
                return False
            
            channel.gain = volume
            return True
            
        except Exception as e:
            print(f"设置音量失败: {e}")
            return False
    
    def list_channels(self) -> dict:
        """
        列出所有可用通道
        
        Returns:
            dict: 通道信息
        """
        if not self.is_connected:
            return {}
        
        try:
            self._vm.invalidate()
            result = {
                "hardware_inputs": [],
                "virtual_inputs": [],
                "hardware_outputs": [],
                "virtual_outputs": []
            }
            
            # 硬件输入
            for i in range(self._vm.phys_in):
                try:
                    ch = self._vm.inputs[i]
                    result["hardware_inputs"].append({
                        "index": i,
                        "name": getattr(ch, 'label', f"Hardware In {i+1}"),
                        "mute": ch.mute,
                        "gain": ch.gain
                    })
                except Exception:
                    pass
            
            # 虚拟输入
            for i in range(self._vm.virt_in):
                try:
                    ch = self._vm.inputs[self._vm.phys_in + i]
                    result["virtual_inputs"].append({
                        "index": i,
                        "name": getattr(ch, 'label', f"Virtual In {i+1}"),
                        "mute": ch.mute,
                        "gain": ch.gain
                    })
                except Exception:
                    pass
            
            # 硬件输出
            for i in range(self._vm.phys_out):
                try:
                    ch = self._vm.outputs[i]
                    result["hardware_outputs"].append({
                        "index": i,
                        "name": getattr(ch, 'label', f"Hardware Out {i+1}"),
                        "mute": ch.mute,
                        "gain": ch.gain
                    })
                except Exception:
                    pass
            
            # 虚拟输出
            for i in range(self._vm.virt_out):
                try:
                    ch = self._vm.outputs[self._vm.phys_out + i]
                    result["virtual_outputs"].append({
                        "index": i,
                        "name": getattr(ch, 'label', f"Virtual Out {i+1}"),
                        "mute": ch.mute,
                        "gain": ch.gain
                    })
                except Exception:
                    pass
            
            return result
            
        except Exception as e:
            print(f"获取通道列表失败: {e}")
            return {}


# 全局实例
_controller: Optional[VoicemeeterController] = None


def get_controller() -> VoicemeeterController:
    """获取全局Voicemeeter控制器实例"""
    global _controller
    if _controller is None:
        _controller = VoicemeeterController()
    return _controller


def toggle_music_mute() -> Optional[bool]:
    """
    快捷函数：切换音乐通道静音
    默认使用虚拟输入通道0（通常是音乐软件输出到的地方）
    
    Returns:
        Optional[bool]: 切换后的静音状态
    """
    ctrl = get_controller()
    if not ctrl.is_connected:
        if not ctrl.connect():
            return None
    return ctrl.toggle_mute("virtual", 0)


def set_music_mute(mute: bool) -> bool:
    """
    快捷函数：设置音乐通道静音
    
    Args:
        mute: 是否静音
    
    Returns:
        bool: 是否成功
    """
    ctrl = get_controller()
    if not ctrl.is_connected:
        if not ctrl.connect():
            return False
    return ctrl.set_mute(mute, "virtual", 0)


if __name__ == '__main__':
    from ui import print_status, print_banner, colorize, Color
    
    print_banner()
    print("Voicemeeter 连接测试")
    print("-" * 40)
    
    ctrl = VoicemeeterController()
    
    if ctrl.connect():
        print_status('success', f"已连接: Voicemeeter {ctrl._vm_type}")
        
        print()
        print("可用通道:")
        channels = ctrl.list_channels()
        
        for ch_type, ch_list in channels.items():
            if ch_list:
                print(f"\n{ch_type}:")
                for ch in ch_list:
                    mute_str = "静音" if ch['mute'] else "启用"
                    print(f"  [{ch['index']}] {ch['name']} - {mute_str}, 音量: {ch['gain']:.2f}")
        
        # 断开连接
        ctrl.disconnect()
    else:
        print_status('error', "无法连接到Voicemeeter")
        print()
        print("请确保:")
        print("1. Voicemeeter已启动")
        print("2. 已安装 voicemeeter-api: pip install voicemeeter-api")
