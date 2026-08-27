# helm++<br>
gpt破甲<br>
单exe  helmx.exe  <br>
原因：之前gpt5.5写poc的时候老是触发安全内容，改5.4有时候能过<br>
直接5.6 又触发审查<br>
<br>
1、网上之前的破甲方法 老是触发审查。<br>
2、helmx原作者的基础上改动了一下，自己用没成功，估计可能是自己方法不对吧<br>
3、根据自己喜好、进行了修改，测试效果还可以。<br>
4、如果skills有问题的话 尽量还是接口deepseek flash修饰下先。

deepseek flash一个源代码审计20快没了 还没审计完。

<br>
使用：<br>
1、只要运行exe就可以了，能自启动的默认都给自启动了,确保web自检里面发送激活、(helmx)✓ 激活确认<br>
2、改写重试-  改写器 Base URL 改成自己的中转平台 ，官方的我没试，用不起！触发了审计，拦截输出，自动给你重写（不是重试、是重写）继续提交，自己前台看不到审计拦截。<br>
3、如果关闭了proxy 需要重启一下codex 
<br>
<img width="1990" height="1015" alt="image" src="https://github.com/user-attachments/assets/5595b895-415f-4fd3-adfb-3b57eb387880" /><br>
<img width="1225" height="641" alt="image" src="https://github.com/user-attachments/assets/a5484533-591a-490e-b462-2b54ad13b947" /><br>
<br>
效果：没有触发哦。yaml质量也还可以<br>
<br>
<img width="1226" height="935" alt="image" src="https://github.com/user-attachments/assets/307842cd-bc08-45b6-8cbd-b3543e15f31c" /><br>
<br>
<img width="1720" height="739" alt="image" src="https://github.com/user-attachments/assets/242b2ebc-a8b5-489b-84d3-b962a4daf473" /><br>
<br>
<img width="1006" height="945" alt="image" src="https://github.com/user-attachments/assets/367b343f-949d-4630-9c00-ae8f827d41f5" /><br>
<br>
<br>
