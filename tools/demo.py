#!/usr/bin/env python3
# Finan+ — dados de demonstração (fictícios) para capturas de tela. SPDX-License-Identifier: GPL-3.0-or-later
import json, datetime as dt
txs=[]; n=0
def add(kind, v, date, desc, cat, paid=True, acc="main", card="", **kw):
    global n; n+=1
    t={"id":f"d{n}","kind":kind,"value":v,"date":date,"desc":desc,"category":cat,"paid":paid,"accountId":acc,"cardId":card}
    t.update(kw); txs.append(t)
for m in [7,8,9,10]:
    mm=f"2026-{m:02d}"
    add("income",5200,f"{mm}-05","Salário","Salário",recurringId="r1")
    add("expense",1500,f"{mm}-10","Aluguel","Moradia",recurringId="r2")
    add("expense",44.90,f"{mm}-08","Netflix","Lazer",card="nu")
    add("expense",21.90,f"{mm}-02","Spotify","Lazer",card="nu")
    add("expense",612.40+m*9,f"{mm}-06","Supermercado Pão de Açúcar","Alimentação")
    add("expense",180+m*3,f"{mm}-15","Conta de luz Enel","Moradia")
    add("expense",95,f"{mm}-12","Academia Fit","Saúde")
    add("expense",38.5,f"{mm}-18","Uber trabalho","Transporte")
    add("expense",120,f"{mm}-21","Restaurante","Alimentação")
add("income",800,"2026-10-20","Freela site","Extra")
# novembro (mês atual)
add("income",5200,"2026-11-05","Salário","Salário",recurringId="r1")
add("expense",1500,"2026-11-10","Aluguel","Moradia",recurringId="r2")
add("expense",55.90,"2026-11-08","Netflix","Lazer",card="nu")
add("expense",21.90,"2026-11-02","Spotify","Lazer",card="nu")
add("expense",289.70,"2026-11-06","Supermercado Pão de Açúcar","Alimentação")
add("expense",23.40,"2026-11-18","Uber casa","Transporte")
add("expense",14.90,"2026-11-24","Padaria São João","Alimentação")
add("expense",199.0,"2026-11-20","Farmácia Drogasil","Saúde")
add("expense",186,"2026-11-28","Conta de luz Enel","Moradia",paid=False)
add("expense",240,"2026-11-22","Internet Vivo Fibra","Moradia",paid=False)
for i,(d,desc) in enumerate([("2026-10-12","Notebook (1/10)"),("2026-11-12","Notebook (2/10)"),("2026-12-12","Notebook (3/10)")]):
    add("expense",459.90,d,desc,"Educação",card="nu",groupId="g1",parcel={"n":i+1,"total":10})
add("expense",900,"2026-10-25","Pagamento fatura Nubank","Pagamento de fatura",cardPayment="nu",cardId="")
add("expense",350,"2026-11-01","Mercado Livre","Outros",card="inter")
# reservas para meta (Android 1.7) e transferência entre contas (Android 1.13)
add("expense",500,"2026-11-06","Meta › Viagem para o Nordeste","Metas",goalId="g-viagem")
add("expense",300,"2026-11-15","Transferência","Transferência",transferId="tr1")
add("income",300,"2026-11-15","Transferência","Transferência",acc="poup",transferId="tr1")
# dívida em parcelas (Meu dinheiro › Dívidas)
for i in range(4):
    add("expense",121.40,f"2026-{10+i if 10+i<=12 else 1:02d}-13" if 10+i<=12 else "2027-01-13",f"Empréstimo Mercado Pago ({i+1}/4)","Dívidas",paid=i<2,groupId="mp",parcel={"n":i+1,"total":4})
state={"txs":txs,
 "goals":[{"id":"g-viagem","name":"Viagem para o Nordeste","target":6000,"saved":2850,"deadline":"2027-06-30","monthly":500},
          {"id":"g-reserva","name":"Reserva de emergência","target":15000,"saved":9800,"deadline":"","monthly":800}],
 "accounts":[{"id":"main","name":"Conta principal","initial":3200},{"id":"poup","name":"Poupança","initial":7500}],
 "cards":[{"id":"nu","name":"Nubank","limit":6000,"close":5,"due":12},{"id":"inter","name":"Inter","limit":3000,"close":25,"due":5}],
 "recurring":[{"id":"r1","kind":"income","desc":"Salário","value":5200,"category":"Salário","accountId":"main","cardId":"","day":5,"active":True,"start":"2026-07-05","last":"2026-11"},
              {"id":"r2","kind":"expense","desc":"Aluguel","value":1500,"category":"Moradia","accountId":"main","cardId":"","day":10,"active":True,"start":"2026-07-10","last":"2026-11"}],
 "shared":[{"id":"s1","name":"Churrasco de aniversário","total":450,"date":"2026-11-21","payer":"p1","split":"equal","note":"",
            "people":[{"id":"p1","name":"Eu","share":150},{"id":"p2","name":"Ana","share":150},{"id":"p3","name":"Pedro","share":150}],
            "payments":[{"id":"pg1","person":"p2","value":150,"date":"2026-11-22"},{"id":"pg2","person":"p3","value":50,"date":"2026-11-23"}]}],
 "cats":{"expense":["Alimentação","Transporte","Moradia","Saúde","Lazer","Educação","Dívidas","Outros"],"income":["Salário","Extra","Investimentos","Outros"]},
 "limits":{"Alimentação":900,"Lazer":120,"Transporte":250},
 "privacy":False,"autoLock":0,"theme":"light","backupVersion":5}
print(json.dumps(state,ensure_ascii=False,indent=1))
