# Referência UMD DirectX

O snapshot público do Windows SDK usado para inspeção está em [tpn/winsdk-10](https://github.com/tpn/winsdk-10/tree/master/Include/10.0.10240.0/um). Os arquivos baixados para análise são `d3dumddi.h`, `d3d10umddi.h` e `d3d12umddi.h`; o caminho presumido para `d3d11umddi.h` não foi encontrado nesse snapshot.

`D3D10DDIARG_OPENADAPTER` contém `hRTAdapter`, `hAdapter`, versão/interface, callbacks de adapter e uma tabela `D3D10DDI_ADAPTERFUNCS`. A tabela base possui `pfnCalcPrivateDeviceSize`, `pfnCreateDevice` e `pfnCloseAdapter`; versões posteriores acrescentam tabelas de capabilities. `D3D12DDIARG_OPENADAPTER` contém `hRTAdapter`, `hAdapter`, callbacks e `D3D12DDI_ADAPTERFUNCS`, cuja tabela inclui cálculo de tamanho, criação/fechamento, versões suportadas, caps, tabelas opcionais e destruição de device.

As assinaturas encontradas no header Windows SDK são:

```cpp
typedef SIZE_T (APIENTRY *PFND3D10DDI_CALCPRIVATEDEVICESIZE)(D3D10DDI_HADAPTER, const D3D10DDIARG_CALCPRIVATEDEVICESIZE*);
typedef HRESULT (APIENTRY *PFND3D10DDI_CREATEDEVICE)(D3D10DDI_HADAPTER, D3D10DDIARG_CREATEDEVICE*);
typedef HRESULT (APIENTRY *PFND3D10DDI_CLOSEADAPTER)(D3D10DDI_HADAPTER);
typedef SIZE_T (APIENTRY *PFND3D12DDI_CALCPRIVATEDEVICESIZE)(D3D12DDI_HADAPTER, const D3D12DDIARG_CALCPRIVATEDEVICESIZE*);
typedef HRESULT (APIENTRY *PFND3D12DDI_CLOSEADAPTER)(D3D12DDI_HADAPTER);
typedef HRESULT (APIENTRY *PFND3D12DDI_CREATEDEVICE_0003)(D3D12DDI_HADAPTER, const D3D12DDIARG_CREATEDEVICE_0003*);
```

A fronteira UMD completa para D3D9/D3D10/D3D11/D3D12 não pode ser obtida apenas com `OpenAdapter`: cada runtime exige tabelas extensas de device functions, recursos, shaders, command lists, heaps, sincronização e callbacks para o KMD. A ativação experimental deve preencher pelo menos as tabelas compatíveis com o header efetivamente instalado no Windows 10/WDK alvo e não deve usar os tipos D3D10 como substitutos silenciosos para D3D11 ou D3D12.
