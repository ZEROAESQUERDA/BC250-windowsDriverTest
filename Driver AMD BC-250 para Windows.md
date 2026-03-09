# Driver AMD BC-250 para Windows

Este projeto visa criar um driver de GPU para Windows compatível com a AMD BC-250 (baseado na arquitetura RDNA2, codinome "Cyan Skillfish"). O driver é implementado usando o Windows Display Driver Model (WDDM) e inclui suporte para DirectX 9, 10, 11 e 12.

## Estrutura do Projeto

O projeto está organizado da seguinte forma:

- `inc/`: Contém arquivos de cabeçalho (headers) com definições de hardware e interfaces DDI (Device Driver Interface) para o Kernel-Mode Driver (KMD) e User-Mode Driver (UMD).
- `src/kmd/`: Contém o código-fonte para o Kernel-Mode Driver (KMD).
- `src/umd/`: Contém o código-fonte para o User-Mode Driver (UMD).
- `inf/`: Contém o arquivo INF para instalação do driver no Windows.
- `build/`: Contém arquivos de projeto para compilação (e.g., MSBuild).
- `tools/`: Contém scripts auxiliares para instalação e desinstalação do driver.
- `docs/`: Contém a documentação técnica do projeto.

## Componentes do Driver

### Kernel-Mode Driver (KMD)

O KMD (`amdbc250_kmd.c`, `amdbc250_hw_init.c`) é responsável pela interação direta com o hardware da GPU. Ele implementa os callbacks WDDM necessários para:

- **Inicialização de Hardware**: Configuração da SMU (System Management Unit), memória, anéis de comando (IH, GFX, SDMA) e o controlador de display (DCN 2.01).
- **Gerenciamento de Interrupções**: Lida com interrupções da GPU, incluindo EOP (End-of-Pipe) e VSYNC.
- **Gerenciamento de Memória de Vídeo**: Alocação e gerenciamento de recursos de memória da GPU.
- **Gerenciamento de Energia**: Suporte a estados de energia D0-D3 via SMU.

### User-Mode Driver (UMD)

O UMD (`amdbc250_umd.c`) é uma DLL carregada pelo runtime do Direct3D. Ele traduz as chamadas da API gráfica (DirectX) para comandos que o KMD pode processar. Atualmente, o UMD inclui suporte para:

- **DirectX 9 DDI**: Implementação básica para renderização e gerenciamento de recursos.
- **DirectX 10 DDI**: Stubs para as funções DDI do D3D10, incluindo criação de blend states, rasterizer states, depth stencil states, shaders, buffers, texturas, etc.
- **DirectX 11 DDI**: Stubs para as funções DDI do D3D11, estendendo o suporte do D3D10 com funcionalidades como compute shaders e unordered access views.
- **DirectX 12 DDI**: Stubs para as funções DDI do D3D12. **É importante notar que a implementação do D3D12 é apenas um placeholder.** Uma implementação completa do D3D12 DDI exige um conhecimento aprofundado da arquitetura RDNA2 e uma programação extensiva do hardware gráfico, o que está além do escopo de um agente de IA automatizado. Cada função precisaria ser implementada para traduzir comandos D3D12 em instruções específicas da GPU e gerenciar recursos da GPU (memória, buffers de comando, shaders, etc.).

### Arquivo INF

O arquivo `amdbc250.inf` contém as informações necessárias para o Windows instalar o driver, incluindo os sete Device IDs PCI conhecidos para as variantes da AMD BC-250 (Cyan Skillfish).

## Compilação

Para compilar o driver, você precisará do **Windows Driver Kit (WDK) 10.0.26100** e do **Visual Studio 2022**.

```powershell
msbuild build\amdbc250kmd.vcxproj /p:Configuration=Release /p:Platform=x64
msbuild build\amdbc250umd.vcxproj /p:Configuration=Release /p:Platform=x64
```

## Instalação

O driver requer que o **test signing** esteja habilitado no Windows (`bcdedit /set testsigning on`), pois não possui assinatura WHQL. A AMD BC-250 foi projetada principalmente para Linux e não possui suporte oficial da AMD no Windows.

Para instalar o driver, execute o script PowerShell `tools/Install-Driver.ps1` como administrador.

## Desinstalação

Para desinstalar o driver, execute o script PowerShell `tools/Uninstall-Driver.ps1` como administrador.

## Contagem de Linhas de Código

O projeto contém um total de **4.844 linhas de código** distribuídas em **12 arquivos**.
