# IMS Tutorial: Consultando Dados com DL/I

Este tutorial mostra como fazer consultas (equivalente a SELECT) no simulador IMS.

## Conceitos Básicos

| SQL       | IMS DL/I  | Descrição                    |
|-----------|-----------|------------------------------|
| SELECT    | GU        | Get Unique - busca direta    |
| SELECT    | GN        | Get Next - busca sequencial  |
| WHERE     | SSA       | Segment Search Argument      |

---

## 1. Iniciando o Simulador

```bash
cd c:\dev\Mainframe-Simulator
.\mainframe-simulator.exe -l
```

O flag `-l` carrega automaticamente o banco HOSPITAL.

---

## 2. Carregar o Banco de Dados

No terminal interativo:
```
/LOAD
```

Mensagem esperada: `HOSPITAL database loaded successfully`

---

## 3. Acessando o Painel DL/I

```
/DLI
```

---

## 4. Consultas com GU (Get Unique)

### Buscar o Segmento Raiz (HOSPITAL)
```
GU HOSPITAL
```
**Resultado:** Retorna o primeiro hospital do banco.

### Buscar com Qualificação (WHERE)
```
GU HOSPITAL (HOSPCODE=H001)
```
**Resultado:** Retorna o hospital com código H001.

### Buscar um Paciente Específico
```
GU HOSPITAL
GN WARD
GN PATIENT (PATNO=P00001)
```

---

## 5. Consultas com GN (Get Next)

### Navegar Sequencialmente
```
GU HOSPITAL          # Posiciona no root
GN WARD              # Primeiro WARD
GN PATIENT           # Primeiro PATIENT
GN PATIENT           # Próximo PATIENT
```

### Listar Todos os Pacientes
```
GU HOSPITAL
GN WARD
GN PATIENT           # Primeiro paciente
GN PATIENT           # Segundo paciente (ou GE = não encontrado)
```

---

## 6. Consultas com GNP (Get Next in Parent)

Busca apenas segmentos filhos do pai atual:

```
GU HOSPITAL
GN WARD              # Posiciona no WARD
GNP PATIENT          # Primeiro PATIENT deste WARD
GNP PATIENT          # Próximo PATIENT deste WARD
```

---

## 7. Status Codes

| Código | Significado                  |
|--------|------------------------------|
| (blank)| Sucesso                      |
| GE     | Segmento não encontrado      |
| GB     | Fim do banco de dados        |
| GA     | Mudou de nível hierárquico   |
| GK     | Mesmo nível, tipo diferente  |

---

## 8. Exemplo Prático Completo

```
# Iniciar e carregar banco
/LOAD

# Entrar no modo DL/I
/DLI

# Buscar hospital
GU HOSPITAL
# Status: OK - HOSPCODE='H001', HOSPNAME='SANTA CASA...'

# Buscar ala de emergência
GN WARD
# Status: OK - WARDNO='W001', WARDNAME='EMERGENCIA'

# Buscar paciente
GN PATIENT
# Status: OK - PATNO='P00001', PATNAME='JOAO DA SILVA'

# Tentar próximo paciente
GN PATIENT
# Status: GE - Segment not found (não há mais pacientes)

# Voltar ao menu
/BACK
```

---

## 9. Comparação SQL vs DL/I

### SQL
```sql
SELECT * FROM PATIENT WHERE PATNO = 'P00001';
```

### IMS DL/I Equivalente
```
GU HOSPITAL
GN WARD
GN PATIENT (PATNO=P00001)
```

> **Nota:** No IMS hierárquico, você precisa navegar pela hierarquia para chegar ao segmento desejado.

---

## 10. Dicas

1. **Sempre comece pelo root** - Use `GU HOSPITAL` primeiro
2. **GN avança sequencialmente** - Cada GN move o ponteiro
3. **GNP respeita o pai** - Não sai do segmento pai atual
4. **Verifique o status** - Se for GE, o segmento não existe

---

## Próximos Passos

- Veja `/HELP` para mais comandos
- Experimente `/DB` para ver a estrutura do banco
- Use `/DISPLAY` para ver estatísticas
